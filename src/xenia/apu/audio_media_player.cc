/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_media_player.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/audio_system.h"
#include "xenia/apu/xma_context.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/guest_scheduler.h"

#include "libretro/core_media_audio.h"

DEFINE_bool(enable_xmp, true, "Enables Music Player playback.", "APU");

namespace xe {
namespace apu {

static int64_t ReadSong(void* owner, uint64_t offset, uint8_t* dst, size_t bytes) {
  vfs::File* file = static_cast<vfs::File*>(owner);
  size_t read = 0;
  if (offset > SIZE_MAX) return -1;
  X_STATUS status = file->ReadSync({dst,bytes}, static_cast<size_t>(offset), &read);
  return (status == X_STATUS_SUCCESS || status == X_STATUS_END_OF_FILE) && read <= bytes
      ? static_cast<int64_t>(read) : -1;
}

static ProcessAudioResult ProcessAudioLoop(AudioMediaPlayer* player, xe_media_audio* audio) {
  const float* pcm;
  size_t frames;
  while (player->IsSongLoaded()) {
    int result = xe_media_audio_read(audio,&pcm,&frames);
    if (result < 0) return ProcessAudioResult::Failure;
    if (!result) return ProcessAudioResult::Successful;
    if (!player->ProcessAudioBuffer(pcm,frames)) return ProcessAudioResult::ForcedFinish;
  }
  return ProcessAudioResult::ForcedFinish;
}

AudioMediaPlayer::AudioMediaPlayer(apu::AudioSystem* audio_system,
                                   kernel::KernelState* kernel_state)
    : audio_system_(audio_system),
      kernel_state_(kernel_state),
      active_playlist_(nullptr),
      active_song_(nullptr) {};

AudioMediaPlayer::~AudioMediaPlayer() {
  Stop();
  // Thread::reset() only closes the handle; must wait for the worker to exit.
  retro_atomic_store_release_int(&worker_running_, 0);
  resume_fence_.Signal();
  if (worker_thread_) {
    xe::threading::Wait(worker_thread_.get(), false);
    worker_thread_.reset();
  }
  DeleteDriver();
};

void AudioMediaPlayer::WorkerThreadMain() {
  while (retro_atomic_load_acquire_int(&worker_running_)) {
    if (!IsPlaying()) {
      resume_fence_.Wait();
    }

    if (!retro_atomic_load_acquire_int(&worker_running_)) break;
    if (!active_playlist_ || !active_song_) {
      // Play/Resume/track changes and shutdown signal this fence. No polling
      // interval or empty-playlist spin is needed while there is no work.
      resume_fence_.Wait();
      continue;
    }

    if (active_song_) {
      Play();
    }
  }
}

void AudioMediaPlayer::Setup() {
  if (!cvars::enable_xmp) {
    return;
  }

  // sample_buffer_ptr_ = kernel_state_->memory()->SystemHeapAlloc(
  //     xe::apu::AudioDriver::kFrameSamplesMax);

  retro_atomic_store_release_int(&worker_running_, 1);
  worker_thread_ = threading::Thread::Create({}, [&] { WorkerThreadMain(); });
  worker_thread_->set_name("Audio Media Player");
};

X_STATUS AudioMediaPlayer::Play(uint32_t playlist_handle, uint32_t song_handle,
                                bool force) {
  auto playlist_itr = playlists_.find(playlist_handle);
  if (playlist_itr == playlists_.cend()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  active_playlist_ = playlist_itr->second.get();

  // I've set it to false (force) for PGR3
  if (!IsIdle()) {
    Stop(false, force);
  }

  if (!song_handle) {
    active_song_ = active_playlist_->songs.cbegin()->get();
    resume_fence_.Signal();
    return X_STATUS_SUCCESS;
  }

  auto song_itr = std::ranges::find_if(
      std::as_const(active_playlist_->songs),
      [song_handle](const std::unique_ptr<XmpApp::Song>& song) {
        return song->handle == song_handle;
      });

  if (song_itr == active_playlist_->songs.cend()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  active_song_ = song_itr->get();
  resume_fence_.Signal();
  return X_STATUS_SUCCESS;
}

void AudioMediaPlayer::Play() {
  uint32_t song_handle = active_song_ ? active_song_->handle : 0;
  vfs::File* file = OpenSong();
  std::unique_ptr<MappedMemory> mapping;
  xe_media_audio* audio = nullptr;
  unsigned rate = 0, channels = 0;
  if (file && file->entry()->can_map())
    mapping = file->entry()->OpenMapped(MappedMemory::Mode::kRead);
  if (mapping && mapping->size() < file->entry()->size()) mapping.reset();
  if (file)
    audio = xe_media_audio_open(mapping ? mapping->data() : nullptr,
        file->entry()->size(), ReadSong, file);
  if (!audio || !xe_media_audio_info(audio,&rate,&channels) || !SetupDriver(rate,channels)) {
    xe_media_audio_close(audio);
    mapping.reset();
    if (file) file->Destroy();
    Stop(true,true);
    processing_end_fence_.Signal();
    return;
  }

  state_ = XmpApp::State::kPlaying;
  current_song_handle_ = song_handle;
  OnStateChanged();

  if (GetVolume() == 0.0f) {
    xe_atomic_float_store(&volume_, kernel_state_->xconfig()->ReadSetting<float>(
        kernel::XCONFIG_USER_CATEGORY, kernel::XCONFIG_USER_MUSIC_VOLUME));
  }

  // Always apply the stored volume to the newly created driver
  driver_->SetVolume(GetVolume());

  auto result =
      ProcessAudioLoop(this, audio);

  // We need to stop playback only if it wasn't
  if (result != ProcessAudioResult::ForcedFinish) {
    Stop(true, true);
  }

  // We're waiting for dangling samples to finish playing.
  if (result == ProcessAudioResult::Successful) {
    xe::threading::Wait(driver_semaphore_.get(), true,
                        std::chrono::milliseconds(500));
  }

  xe_media_audio_close(audio);
  mapping.reset();
  file->Destroy();

  processing_end_fence_.Signal();

  if (result == ProcessAudioResult::ForcedFinish) {
    DeleteDriver();
    return;
  }

  if (!IsLastSongInPlaylist()) {
    Next();
    return;
  }

  // We're after last song in playlist
  if (IsInRepeatMode()) {
    Play(active_playlist_->handle, 0, true);
  };
}

void AudioMediaPlayer::Pause() {
  if (!IsPlaying()) {
    return;
  }

  if (driver_) {
    driver_->Pause();
  }
  state_ = XmpApp::State::kPaused;
  OnStateChanged();
};

void AudioMediaPlayer::Stop(bool change_state, bool force) {
  if (IsIdle()) {
    return;
  }

  if (driver_ && IsPaused()) {
    driver_->Resume();
  }

  state_ = XmpApp::State::kIdle;
  active_song_ = nullptr;

  if (!force) {
    // Reachable from the guest XMP shim, so on a fiber this must park rather
    // than block: a plain fence wait would stall the whole dispatch CPU,
    // including the fibers that could satisfy it.
    kernel::GuestScheduler::WaitOnFence(processing_end_fence_);
  }

  if (change_state) {
    OnStateChanged();
  }
};

void AudioMediaPlayer::Continue() {
  if (!IsPaused()) {
    return;
  }

  state_ = XmpApp::State::kPlaying;
  resume_fence_.Signal();
  if (driver_) {
    driver_->Resume();
  }
  OnStateChanged();
}

X_STATUS AudioMediaPlayer::Next() {
  if (!active_playlist_) {
    return X_STATUS_UNSUCCESSFUL;
  }

  if (active_song_) {
    Stop(false, false);
  }

  auto itr =
      std::ranges::find_if(std::as_const(active_playlist_->songs),
                           [this](const std::unique_ptr<XmpApp::Song>& song) {
                             return song->handle == current_song_handle_;
                           });

  // There is no song with such ID?
  if (itr == active_playlist_->songs.cend()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  itr = std::next(itr);

  if (itr != active_playlist_->songs.cend()) {
    active_song_ = itr->get();
    resume_fence_.Signal();
    return X_STATUS_SUCCESS;
  }

  active_song_ = active_playlist_->songs.cbegin()->get();
  resume_fence_.Signal();
  return X_STATUS_SUCCESS;
}

X_STATUS AudioMediaPlayer::Previous() {
  if (!active_playlist_) {
    return X_STATUS_UNSUCCESSFUL;
  }

  if (active_song_) {
    Stop(false, false);
  }

  auto itr =
      std::ranges::find_if(std::as_const(active_playlist_->songs),
                           [this](const std::unique_ptr<XmpApp::Song>& song) {
                             return song->handle == current_song_handle_;
                           });

  // We're at the first entry, we need to go to the end.
  if (itr == active_playlist_->songs.cbegin()) {
    active_song_ = active_playlist_->songs.crbegin()->get();
    resume_fence_.Signal();
    return X_STATUS_SUCCESS;
  }

  itr = std::prev(itr);
  active_song_ = itr->get();
  resume_fence_.Signal();
  return X_STATUS_SUCCESS;
}

vfs::File* AudioMediaPlayer::OpenSong() {
  vfs::File* file = nullptr;
  vfs::FileAction action;
  if (!active_song_) return nullptr;
  X_STATUS result = kernel_state_->file_system()->OpenFile(
      nullptr, xe::to_utf8(active_song_->file_path), vfs::FileDisposition::kOpen,
      vfs::FileAccess::kGenericRead, false, true, &file, &action);
  return result == X_STATUS_SUCCESS ? file : nullptr;
}

void AudioMediaPlayer::AddPlaylist(uint32_t handle,
                                   std::unique_ptr<XmpApp::Playlist> playlist) {
  if (playlists_.count(handle) != 0) {
    return;
  }

  if (playback_mode_ == XmpApp::PlaybackMode::kShuffle) {
    auto rng = std::default_random_engine{};
    std::ranges::shuffle(playlist->songs, rng);
  }

  playlists_.insert({handle, std::move(playlist)});
}

void AudioMediaPlayer::RemovePlaylist(uint32_t handle) {
  if (playlists_.count(handle) == 0) {
    return;
  }

  // TODO: Check if currently played song is from that playlist and stop
  // playback.
  if (active_playlist_ && active_song_) {
    Stop();
  }

  playlists_.erase(handle);
}

X_STATUS AudioMediaPlayer::SetVolume(float volume) {
  xe_atomic_float_store(&volume_, std::min(volume, 1.0f));

  std::unique_lock<xe_mutex> guard(driver_mutex_);
  if (!driver_) {
    return X_STATUS_UNSUCCESSFUL;
  }

  driver_->SetVolume(GetVolume());
  return X_STATUS_SUCCESS;
}

bool AudioMediaPlayer::IsLastSongInPlaylist() const {
  if (!active_playlist_) {
    return false;
  }

  auto itr =
      std::ranges::find_if(std::as_const(active_playlist_->songs),
                           [this](const std::unique_ptr<XmpApp::Song>& song) {
                             return song->handle == current_song_handle_;
                           });
  itr = std::next(itr);
  return itr == active_playlist_->songs.cend();
}

void AudioMediaPlayer::SetCaptureCallback(uint32_t callback, uint32_t context,
                                          bool title_render) {
  // TODO: Something is incorrect with callback and causes audio to be muted!
  callback_ = 0;
  callback_context_ = context;
  is_title_rendering_enabled_ = false;  // title_render;
}

void AudioMediaPlayer::OnStateChanged() {
  kernel_state_->BroadcastNotification(kXNotificationXmpStateChanged,
                                       static_cast<uint32_t>(state_));
}

bool AudioMediaPlayer::ProcessAudioBuffer(const float* samples, size_t frames) {
  xe::threading::Wait(driver_semaphore_.get(), true);
  if (!IsSongLoaded()) return false;
  return driver_->SubmitInterleavedFrames(samples, static_cast<uint32_t>(frames));
}

bool AudioMediaPlayer::SetupDriver(uint32_t sample_rate, uint32_t channels) {
  DeleteDriver();

  std::unique_lock<xe_mutex> guard(driver_mutex_);
  driver_semaphore_ = xe::threading::Semaphore::Create(
      AudioSystem::kMaximumQueuedFrames, AudioSystem::kMaximumQueuedFrames);

  if (!driver_semaphore_) {
    return false;
  }

  driver_ = std::unique_ptr<AudioDriver>(audio_system_->CreateDriver(
      driver_semaphore_.get(), sample_rate, channels, false));

  if (!driver_) {
    driver_semaphore_.reset();
    return false;
  }

  if (!driver_->Initialize()) {
    driver_semaphore_.reset();
    driver_->Shutdown();
    driver_.reset();
    return false;
  }

  return true;
}

void AudioMediaPlayer::DeleteDriver() {
  std::unique_lock<xe_mutex> guard(driver_mutex_);
  if (driver_) {
    if (driver_semaphore_) {
      driver_semaphore_.reset();
    }

    driver_->Shutdown();
    driver_.reset();
  }
}

}  // namespace apu
}  // namespace xe
