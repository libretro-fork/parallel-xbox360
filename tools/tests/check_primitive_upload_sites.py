"""Supplement recipe tests with the production Vulkan allocation handoff.

This is a source guard, not device-backed initialization/lifetime acceptance.
The C89 recipe requires the entire upload allocation mapped at offset zero.
"""
import re
from pathlib import Path
import unittest

from check_draw_event_sites import masked, closing

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/xenia/gpu/vulkan/vulkan_primitive_processor.cc'


def arguments(text, name):
    found = list(re.finditer(r'\b' + re.escape(name) + r'\s*\(', text))
    if len(found) != 1:
        raise ValueError('expected exactly one ' + name)
    start = text.index('(', found[0].start())
    end = closing(text, start)
    result, depth, begin = [], 0, start + 1
    for i in range(start + 1, end - 1):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
        elif text[i] == ',' and not depth:
            result.append(re.sub(r'\s+', '', text[begin:i]))
            begin = i + 1
    result.append(re.sub(r'\s+', '', text[begin:end - 1]))
    return result, found[0].start(), end


def inspect(source):
    text = masked(source)
    name = 'bool VulkanPrimitiveProcessor::InitializeBuiltinIndexBuffer'
    start = text.index(name)
    text = text[start:text.index('void* VulkanPrimitiveProcessor::', start)]
    mapping, _, _ = arguments(text, 'vkMapMemory')
    flush, _, end = arguments(text, 'xe_vk_upload_flush')
    unmap, unmap_at, _ = arguments(text, 'vkUnmapMemory')
    upload = 'builtin_index_buffer_upload_memory_'
    if mapping != ['device', upload, '0', 'VK_WHOLE_SIZE', '0', '&mapping']:
        raise ValueError('upload recipe requires a whole upload-allocation map')
    if flush != ['dfn.vkFlushMappedMemoryRanges', 'device', upload,
                 'upload_memory_type', 'vulkan_device->memory_types().host_coherent']:
        raise ValueError('flush must borrow the mapped upload allocation/type')
    if unmap != ['device', upload] or unmap_at < end:
        raise ValueError('unmap must follow flushing the same upload allocation')
    if not re.match(r'\s*==\s*VK_SUCCESS\s*;', text[end:]):
        raise ValueError('flush driver failure must be propagated')
    failure = text.index('if (!filled)', unmap_at)
    publication = text.index('builtin_index_buffer_upload_submission_', failure)
    cleanup = text[failure:publication]
    for handle in ['builtin_index_buffer_upload_', upload,
                   'builtin_index_buffer_', 'builtin_index_buffer_memory_']:
        if not re.search(r'\b' + handle + r'\s*\)', cleanup):
            raise ValueError('missing failed-upload cleanup: ' + handle)
    if 'return false;' not in cleanup:
        raise ValueError('failed upload must return before publication')


class UploadWiring(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text(encoding='utf-8')

    def test_actual_backend(self):
        inspect(self.source)

    def test_wrong_flush_allocation(self):
        bad = self.source.replace(
            'builtin_index_buffer_upload_memory_, upload_memory_type,',
            'builtin_index_buffer_memory_, upload_memory_type,')
        self.assertNotEqual(bad, self.source)
        with self.assertRaises(ValueError):
            inspect(bad)

    def test_partial_mapping(self):
        bad = self.source.replace('VK_WHOLE_SIZE, 0, &mapping', 'size_bytes, 0, &mapping')
        self.assertNotEqual(bad, self.source)
        with self.assertRaises(ValueError):
            inspect(bad)

    def test_discarded_driver_error(self):
        bad = self.source.replace(') == VK_SUCCESS;', ') != VK_ERROR_OUT_OF_HOST_MEMORY;')
        self.assertNotEqual(bad, self.source)
        with self.assertRaises(ValueError):
            inspect(bad)

    def test_wrong_unmap_allocation(self):
        bad = self.source.replace(
            'vkUnmapMemory(device, builtin_index_buffer_upload_memory_)',
            'vkUnmapMemory(device, builtin_index_buffer_memory_)')
        self.assertNotEqual(bad, self.source)
        with self.assertRaises(ValueError):
            inspect(bad)


if __name__ == '__main__':
    unittest.main()
