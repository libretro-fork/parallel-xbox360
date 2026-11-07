/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/d3d12/deferred_command_list.h"

#include <new>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/d3d12/pipeline_cache.h"
#include "xenia/gpu/gpu_flags.h"

namespace xe {
namespace gpu {
namespace d3d12 {

DeferredCommandList::DeferredCommandList(size_t initial_size) {
  if (!xe_command_stream_init(&command_stream_, initial_size) ||
      !xe_command_rows_init(&barrier_rows_, sizeof(D3D12_RESOURCE_BARRIER),
                            64 * 1024)) throw std::bad_alloc();
  static_assert(sizeof(D3D12_RESOURCE_BARRIER) % sizeof(uintmax_t) == 0);
  static_assert(alignof(D3D12_RESOURCE_BARRIER) <= alignof(uintmax_t));
}

void DeferredCommandList::Reset() {
  xe_command_stream_reset(&command_stream_);
  // Preserve any barriers prepared outside an open submission, as before.
  xe_command_rows_reset(&barrier_rows_);
}

D3D12_RESOURCE_BARRIER& DeferredCommandList::AppendResourceBarrier() {
  void* row = xe_command_rows_append(&barrier_rows_);
  if (!row) throw std::bad_alloc();
  return *new (row) D3D12_RESOURCE_BARRIER;
}

void DeferredCommandList::CancelResourceBarrier() {
  if (!xe_command_rows_pop(&barrier_rows_)) throw std::bad_alloc();
}

int DeferredCommandList::EmitResourceBarriers(void* context,
                                               const xe_command_row_view* view) {
  auto* self = static_cast<DeferredCommandList*>(context);
  void* storage = self->WriteCommand(Command::kBorrowedResourceBarriers,
                                     sizeof(BorrowedResourceBarriers));
  auto& args = *new (storage) BorrowedResourceBarriers;
  args.data = static_cast<const D3D12_RESOURCE_BARRIER*>(view->data);
  args.count = view->count;
  return 1;
}

void DeferredCommandList::SubmitResourceBarriers() {
  if (!xe_command_rows_submit(&barrier_rows_, UINT32_MAX,
                              EmitResourceBarriers, this)) throw std::bad_alloc();
}
void DeferredCommandList::Execute(ID3D12GraphicsCommandList* command_list,
                                  ID3D12GraphicsCommandList1* command_list_1,
                                  ID3D12GraphicsCommandList2* command_list_2) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES
  ID3D12PipelineState* current_pipeline_state = nullptr;
  for (const xe_command_page* page = command_stream_.first; page; page = page->next) {
    ExecutePage(page, command_list, command_list_1, command_list_2,
                current_pipeline_state);
  }
}

void DeferredCommandList::ExecutePage(
    const xe_command_page* page, ID3D12GraphicsCommandList* command_list,
    ID3D12GraphicsCommandList1* command_list_1,
    ID3D12GraphicsCommandList2* command_list_2,
    ID3D12PipelineState*& current_pipeline_state) {
  const uintmax_t* stream = reinterpret_cast<const uintmax_t*>(xe_command_page_const_data(page));
  size_t stream_remaining = page->used / sizeof(uintmax_t);
  while (stream_remaining != 0) {
    const CommandHeader& header =
        *reinterpret_cast<const CommandHeader*>(stream);
    stream += kCommandHeaderSizeElements;
    stream_remaining -= kCommandHeaderSizeElements;
    switch (header.command) {
      case Command::kD3DClearDepthStencilView: {
        auto& args =
            *reinterpret_cast<const ClearDepthStencilViewHeader*>(stream);
        command_list->ClearDepthStencilView(
            args.depth_stencil_view, args.clear_flags, args.depth, args.stencil,
            args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1)
                           : nullptr);
      } break;
      case Command::kD3DClearRenderTargetView: {
        auto& args =
            *reinterpret_cast<const ClearRenderTargetViewHeader*>(stream);
        command_list->ClearRenderTargetView(
            args.render_target_view, args.color_rgba, args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1)
                           : nullptr);
      } break;
      case Command::kD3DClearUnorderedAccessViewUint: {
        auto& args =
            *reinterpret_cast<const ClearUnorderedAccessViewHeader*>(stream);
        command_list->ClearUnorderedAccessViewUint(
            args.view_gpu_handle_in_current_heap, args.view_cpu_handle,
            args.resource, args.values_uint, args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1)
                           : nullptr);
      } break;
      case Command::kD3DCopyBufferRegion: {
        auto& args =
            *reinterpret_cast<const D3DCopyBufferRegionArguments*>(stream);
        command_list->CopyBufferRegion(args.dst_buffer, args.dst_offset,
                                       args.src_buffer, args.src_offset,
                                       args.num_bytes);
      } break;
      case Command::kD3DCopyResource: {
        auto& args = *reinterpret_cast<const D3DCopyResourceArguments*>(stream);
        command_list->CopyResource(args.dst_resource, args.src_resource);
      } break;
      case Command::kCopyTexture: {
        auto& args = *reinterpret_cast<const CopyTextureArguments*>(stream);
        command_list->CopyTextureRegion(&args.dst, 0, 0, 0, &args.src, nullptr);
      } break;
      case Command::kD3DCopyTextureRegion: {
        auto& args =
            *reinterpret_cast<const D3DCopyTextureRegionArguments*>(stream);
        command_list->CopyTextureRegion(
            &args.dst, args.dst_x, args.dst_y, args.dst_z, &args.src,
            args.has_src_box ? &args.src_box : nullptr);
      } break;
      case Command::kD3DDispatch: {
        if (current_pipeline_state != nullptr) {
          auto& args = *reinterpret_cast<const D3DDispatchArguments*>(stream);
          command_list->Dispatch(args.thread_group_count_x,
                                 args.thread_group_count_y,
                                 args.thread_group_count_z);
        }
      } break;
      case Command::kD3DDrawIndexedInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DDrawIndexedInstancedArguments*>(
                  stream);
          command_list->DrawIndexedInstanced(
              args.index_count_per_instance, args.instance_count,
              args.start_index_location, args.base_vertex_location,
              args.start_instance_location);
        }
      } break;
      case Command::kD3DDrawInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DDrawInstancedArguments*>(stream);
          command_list->DrawInstanced(
              args.vertex_count_per_instance, args.instance_count,
              args.start_vertex_location, args.start_instance_location);
        }
      } break;
      case Command::kD3DBeginQuery: {
        auto& args = *reinterpret_cast<const D3DQueryArguments*>(stream);
        command_list->BeginQuery(args.query_heap, args.query_type,
                                 args.query_index);
      } break;
      case Command::kD3DEndQuery: {
        auto& args = *reinterpret_cast<const D3DQueryArguments*>(stream);
        command_list->EndQuery(args.query_heap, args.query_type,
                               args.query_index);
      } break;
      case Command::kD3DResolveQueryData: {
        auto& args =
            *reinterpret_cast<const D3DResolveQueryDataArguments*>(stream);
        command_list->ResolveQueryData(
            args.query_heap, args.query_type, args.start_index,
            args.query_count, args.destination_buffer, args.destination_offset);
      } break;
      case Command::kD3DSetPredication: {
        auto& args =
            *reinterpret_cast<const D3DSetPredicationArguments*>(stream);
        command_list->SetPredication(args.buffer, args.aligned_buffer_offset,
                                     args.operation);
      } break;
      case Command::kD3DIASetIndexBuffer: {
        auto view = reinterpret_cast<const D3D12_INDEX_BUFFER_VIEW*>(stream);
        command_list->IASetIndexBuffer(
            view->Format != DXGI_FORMAT_UNKNOWN ? view : nullptr);
      } break;
      case Command::kD3DIASetPrimitiveTopology: {
        command_list->IASetPrimitiveTopology(
            *reinterpret_cast<const D3D12_PRIMITIVE_TOPOLOGY*>(stream));
      } break;
      case Command::kD3DIASetVertexBuffers: {
        static_assert(alignof(D3D12_VERTEX_BUFFER_VIEW) <= alignof(uintmax_t));
        auto& args =
            *reinterpret_cast<const D3DIASetVertexBuffersHeader*>(stream);
        command_list->IASetVertexBuffers(
            args.start_slot, args.num_views,
            reinterpret_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                reinterpret_cast<const uint8_t*>(stream) +
                xe::align(sizeof(D3DIASetVertexBuffersHeader),
                          alignof(D3D12_VERTEX_BUFFER_VIEW))));
      } break;
      case Command::kD3DOMSetBlendFactor: {
        command_list->OMSetBlendFactor(reinterpret_cast<const FLOAT*>(stream));
      } break;
      case Command::kD3DOMSetRenderTargets: {
        auto& args =
            *reinterpret_cast<const D3DOMSetRenderTargetsArguments*>(stream);
        command_list->OMSetRenderTargets(
            args.num_render_target_descriptors, args.render_target_descriptors,
            args.rts_single_handle_to_descriptor_range ? true : false,
            args.depth_stencil ? &args.depth_stencil_descriptor : nullptr);
      } break;
      case Command::kD3DOMSetStencilRef: {
        command_list->OMSetStencilRef(*reinterpret_cast<const UINT*>(stream));
      } break;
      case Command::kD3DResourceBarrier: {
        static_assert(alignof(D3D12_RESOURCE_BARRIER) <= alignof(uintmax_t));
        command_list->ResourceBarrier(
            *reinterpret_cast<const UINT*>(stream),
            reinterpret_cast<const D3D12_RESOURCE_BARRIER*>(
                reinterpret_cast<const uint8_t*>(stream) +
                xe::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER))));
      } break;
      case Command::kBorrowedResourceBarriers: {
        const auto& args = *reinterpret_cast<const BorrowedResourceBarriers*>(stream);
        command_list->ResourceBarrier(args.count, args.data);
      } break;
      case Command::kRSSetScissorRect: {
        command_list->RSSetScissorRects(
            1, reinterpret_cast<const D3D12_RECT*>(stream));
      } break;
      case Command::kRSSetViewport: {
        command_list->RSSetViewports(
            1, reinterpret_cast<const D3D12_VIEWPORT*>(stream));
      } break;
      case Command::kD3DSetComputeRoot32BitConstants: {
        auto args =
            reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetComputeRoot32BitConstants(
            args->root_parameter_index, args->num_32bit_values_to_set, args + 1,
            args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetGraphicsRoot32BitConstants: {
        auto args =
            reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetGraphicsRoot32BitConstants(
            args->root_parameter_index, args->num_32bit_values_to_set, args + 1,
            args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetComputeRootConstantBufferView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetComputeRootConstantBufferView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootConstantBufferView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetGraphicsRootConstantBufferView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetComputeRootDescriptorTable: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetComputeRootDescriptorTable(args.root_parameter_index,
                                                    args.base_descriptor);
      } break;
      case Command::kD3DSetGraphicsRootDescriptorTable: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetGraphicsRootDescriptorTable(args.root_parameter_index,
                                                     args.base_descriptor);
      } break;
      case Command::kD3DSetComputeRootShaderResourceView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetComputeRootShaderResourceView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootShaderResourceView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetGraphicsRootShaderResourceView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetComputeRootSignature: {
        command_list->SetComputeRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kD3DSetGraphicsRootSignature: {
        command_list->SetGraphicsRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kD3DSetComputeRootUnorderedAccessView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetComputeRootUnorderedAccessView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootUnorderedAccessView: {
        auto& args =
            *reinterpret_cast<const SetRootDescriptorArguments*>(stream);
        command_list->SetGraphicsRootUnorderedAccessView(
            args.root_parameter_index, args.buffer_location);
      } break;
      case Command::kSetDescriptorHeaps: {
        auto& args =
            *reinterpret_cast<const SetDescriptorHeapsArguments*>(stream);
        UINT num_descriptor_heaps = 0;
        ID3D12DescriptorHeap* descriptor_heaps[2];
        if (args.cbv_srv_uav_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] =
              args.cbv_srv_uav_descriptor_heap;
        }
        if (args.sampler_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] =
              args.sampler_descriptor_heap;
        }
        command_list->SetDescriptorHeaps(num_descriptor_heaps,
                                         descriptor_heaps);
      } break;
      case Command::kD3DSetPipelineState: {
        current_pipeline_state =
            *reinterpret_cast<ID3D12PipelineState* const*>(stream);
        if (current_pipeline_state) {
          command_list->SetPipelineState(current_pipeline_state);
        }
      } break;
      case Command::kSetPipelineStateHandle: {
        current_pipeline_state = PipelineCache::GetD3D12PipelineByHandle(
            *reinterpret_cast<void* const*>(stream));
        if (current_pipeline_state) {
          command_list->SetPipelineState(current_pipeline_state);
        }
      } break;
      case Command::kD3DSetSamplePositions: {
        if (command_list_1 != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DSetSamplePositionsArguments*>(stream);
          command_list_1->SetSamplePositions(
              args.num_samples_per_pixel, args.num_pixels,
              (args.num_samples_per_pixel && args.num_pixels)
                  ? const_cast<D3D12_SAMPLE_POSITION*>(args.sample_positions)
                  : nullptr);
        }
      } break;
      case Command::kD3DWriteBufferImmediate: {
        if (command_list_2 != nullptr) {
          auto& args =
              *reinterpret_cast<const D3DWriteBufferImmediateArguments*>(
                  stream);
          D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param;
          param.Dest = args.dest;
          param.Value = args.value;
          D3D12_WRITEBUFFERIMMEDIATE_MODE mode =
              D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT;
          command_list_2->WriteBufferImmediate(1, &param, &mode);
        }
      } break;
      case Command::kBeginDebugMarker: {
        auto& args = *reinterpret_cast<const DebugMarkerHeader*>(stream);
        const char* label_name = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(stream) +
            sizeof(DebugMarkerHeader));
        // PIX_EVENT_ANSI_VERSION metadata (0x1 for ANSI string).
        command_list->BeginEvent(1, label_name,
                                 static_cast<UINT>(args.label_length + 1));
      } break;
      case Command::kEndDebugMarker: {
        command_list->EndEvent();
      } break;
      case Command::kInsertDebugMarker: {
        auto& args = *reinterpret_cast<const DebugMarkerHeader*>(stream);
        const char* label_name = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(stream) +
            sizeof(DebugMarkerHeader));
        // PIX_EVENT_ANSI_VERSION metadata (0x1 for ANSI).
        command_list->SetMarker(1, label_name,
                                static_cast<UINT>(args.label_length + 1));
      } break;
      default:
        assert_unhandled_case(header.command);
        break;
    }
    stream += header.arguments_size_elements;
    stream_remaining -= header.arguments_size_elements;
  }
}

void* DeferredCommandList::WriteCommand(Command command,
                                        size_t arguments_size_bytes) {
  uint32_t argument_elements;
  size_t packet_bytes;
  constexpr size_t kCommandHeaderSizeBytes =
      kCommandHeaderSizeElements * sizeof(uintmax_t);
  if (!xe_command_packet_layout(arguments_size_bytes, kCommandHeaderSizeBytes,
                                &argument_elements, &packet_bytes)) throw std::bad_alloc();
  uint8_t* packet = static_cast<uint8_t*>(xe_command_stream_append(&command_stream_, packet_bytes));
  if (!packet) throw std::bad_alloc();
  CommandHeader& header = *reinterpret_cast<CommandHeader*>(packet);
  header.command = command;
  header.arguments_size_elements = argument_elements;
  return packet + kCommandHeaderSizeBytes;
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe
