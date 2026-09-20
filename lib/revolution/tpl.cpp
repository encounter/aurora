#include <aurora/tpl.h>
#include "../logging.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
constexpr u32 TplVersion = 2142000;
constexpr size_t PaletteSize = 12;
constexpr size_t DescriptorSize = 8;
constexpr size_t TextureHeaderSize = 36;
constexpr size_t ClutHeaderSize = 12;
constexpr aurora::Module Log{"aurora::tpl"};

struct Palette {
	TPLPalette header{};
	std::vector<TPLDescriptor> descriptors;
	std::map<u32, TPLHeader> textures;
	std::map<u32, TPLClutHeader> cluts;
	size_t requiredSize = PaletteSize;
};

std::mutex paletteMutex;
std::unordered_map<const void*, std::unique_ptr<Palette>> palettes;

u16 read_u16(const u8* data) {
	return static_cast<u16>((static_cast<u16>(data[0]) << 8) | data[1]);
}

u32 read_u32(const u8* data) {
	return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) | (static_cast<u32>(data[2]) << 8) |
		   data[3];
}

bool check_range(Palette& palette, size_t size, size_t offset, size_t length) {
	if (offset > size || length > size - offset) {
		return false;
	}

	palette.requiredSize = std::max(palette.requiredSize, offset + length);

	return true;
}

bool texture_size(const TPLHeader& header, size_t& size) {
	u32 blockWidth = 4;
	u32 blockHeight = 4;
	u32 blockBytes = 32;

	switch (header.format) {
	case GX_TF_I4:
	case GX_TF_C4:
	case GX_TF_CMPR:
		blockWidth = 8;
		blockHeight = 8;
		break;
	case GX_TF_I8:
	case GX_TF_IA4:
	case GX_TF_C8:
		blockWidth = 8;
		break;
	case GX_TF_IA8:
	case GX_TF_RGB565:
	case GX_TF_RGB5A3:
	case GX_TF_C14X2:
		break;
	case GX_TF_RGBA8:
		blockBytes = 64;
		break;
	default:
		return false;
	}

	u32 width = header.width;
	u32 height = header.height;
	size_t total = 0;

	for (u32 level = 0; level <= header.maxLOD; ++level) {
		const u64 blocksX = (width + blockWidth - 1) / blockWidth;
		const u64 blocksY = (height + blockHeight - 1) / blockHeight;
		const u64 bytes = blocksX * blocksY * blockBytes;

		if (bytes > std::numeric_limits<size_t>::max() - total) {
			return false;
		}

		total += static_cast<size_t>(bytes);
		width = std::max(width / 2, 1u);
		height = std::max(height / 2, 1u);
	}

	size = total;

	return true;
}

bool read_texture(Palette& palette, u8* data, size_t size, u32 offset) {
	if (palette.textures.contains(offset)) {
		return true;
	}

	if (offset < PaletteSize || !check_range(palette, size, offset, TextureHeaderSize)) {
		return false;
	}

	const auto* source = data + offset;
	TPLHeader header{};
	header.height = read_u16(source);
	header.width = read_u16(source + 2);
	header.format = read_u32(source + 4);
	const u32 dataOffset = read_u32(source + 8);
	const u32 wrapS = read_u32(source + 12);
	const u32 wrapT = read_u32(source + 16);
	const u32 minFilter = read_u32(source + 20);
	const u32 magFilter = read_u32(source + 24);
	header.LODBias = std::bit_cast<float>(read_u32(source + 28));
	header.edgeLODEnable = source[32];
	header.minLOD = source[33];
	header.maxLOD = source[34];
	header.unpacked = 1;

	if (!header.width || !header.height || header.minLOD > header.maxLOD || !std::isfinite(header.LODBias) ||
		wrapS > GX_MIRROR || wrapT > GX_MIRROR || minFilter > GX_LIN_MIP_LIN || magFilter > GX_LINEAR ||
		header.edgeLODEnable > 1) {
		return false;
	}

	header.wrapS = static_cast<GXTexWrapMode>(wrapS);
	header.wrapT = static_cast<GXTexWrapMode>(wrapT);
	header.minFilter = static_cast<GXTexFilter>(minFilter);
	header.magFilter = static_cast<GXTexFilter>(magFilter);

	size_t bytes = 0;

	if (!texture_size(header, bytes) || dataOffset < PaletteSize || !check_range(palette, size, dataOffset, bytes)) {
		return false;
	}

	header.data = reinterpret_cast<char*>(data + dataOffset);
	palette.textures.emplace(offset, header);

	return true;
}

bool read_clut(Palette& palette, u8* data, size_t size, u32 offset) {
	if (palette.cluts.contains(offset)) {
		return true;
	}

	if (offset < PaletteSize || !check_range(palette, size, offset, ClutHeaderSize)) {
		return false;
	}

	const auto* source = data + offset;
	TPLClutHeader header{};
	header.numEntries = read_u16(source);
	header.unpacked = 1;
	header._4 = source[3];
	const u32 format = read_u32(source + 4);
	const u32 dataOffset = read_u32(source + 8);

	if (format > GX_TL_RGB5A3 || dataOffset < PaletteSize ||
		!check_range(palette, size, dataOffset, static_cast<size_t>(header.numEntries) * 2)) {
		return false;
	}

	header.format = static_cast<GXTlutFmt>(format);
	header.data = reinterpret_cast<char*>(data + dataOffset);
	palette.cluts.emplace(offset, header);

	return true;
}

std::unique_ptr<Palette> read_palette(u8* data, size_t size) {
	if (size < PaletteSize || read_u32(data) != TplVersion) {
		return nullptr;
	}

	const u32 count = read_u32(data + 4);
	const u32 descriptorOffset = read_u32(data + 8);

	if (descriptorOffset < PaletteSize || descriptorOffset > size ||
		count > (size - descriptorOffset) / DescriptorSize) {
		return nullptr;
	}

	auto palette = std::make_unique<Palette>();
	palette->header.versionNumber = TplVersion;
	palette->header.numDescriptors = count;
	palette->requiredSize = descriptorOffset + static_cast<size_t>(count) * DescriptorSize;
	palette->descriptors.resize(count);

	for (u32 i = 0; i < count; ++i) {
		const auto* source = data + descriptorOffset + static_cast<size_t>(i) * DescriptorSize;
		const u32 textureOffset = read_u32(source);
		const u32 clutOffset = read_u32(source + 4);
		auto& descriptor = palette->descriptors[i];

		if (textureOffset) {
			if (!read_texture(*palette, data, size, textureOffset)) {
				return nullptr;
			}

			descriptor.textureHeader = &palette->textures.at(textureOffset);
		}

		if (clutOffset) {
			if (!read_clut(*palette, data, size, clutOffset)) {
				return nullptr;
			}

			descriptor.CLUTHeader = &palette->cluts.at(clutOffset);
		}
	}

	palette->header.descriptorArray = palette->descriptors.data();

	return palette;
}
} // namespace

BOOL aurora_tpl_bind(void* data, size_t size) {
	if (!data) {
		return FALSE;
	}

	std::lock_guard lock(paletteMutex);
	const auto existing = palettes.find(data);

	if (existing != palettes.end()) {
		return existing->second->requiredSize <= size;
	}

	try {
		auto palette = read_palette(static_cast<u8*>(data), size);

		if (!palette) {
			return FALSE;
		}

		palettes.emplace(data, std::move(palette));
	} catch (const std::bad_alloc&) {
		return FALSE;
	} catch (const std::length_error&) {
		return FALSE;
	}

	return TRUE;
}

const TPLPalette* aurora_tpl_get_palette(const void* data) {
	std::lock_guard lock(paletteMutex);
	const auto entry = palettes.find(data);

	if (entry == palettes.end()) {
		return nullptr;
	}

	return &entry->second->header;
}

void aurora_tpl_unbind(const void* data) {
	std::lock_guard lock(paletteMutex);
	palettes.erase(data);
}

void TPLBind(TPLPalettePtr ptr) {
	if (!aurora_tpl_bind(ptr, std::numeric_limits<u32>::max())) {
		Log.fatal("Invalid texture palette");
	}
}

TPLDescriptorPtr TPLGet(TPLPalettePtr ptr, u32 id) {
	std::lock_guard lock(paletteMutex);
	const auto entry = palettes.find(ptr);

	if (entry == palettes.end() || entry->second->descriptors.empty()) {
		return nullptr;
	}

	auto& descriptors = entry->second->descriptors;

	return &descriptors[id % descriptors.size()];
}
