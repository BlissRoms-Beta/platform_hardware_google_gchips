/*
 * Copyright (C) 2020 Arm Limited.
 *
 * Copyright 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "MapperMetadata.h"
#include "SharedMetadata.h"
#include "core/format_info.h"
#include "core/mali_gralloc_bufferallocation.h"
#include "mali_gralloc_buffer.h"
#include "mali_gralloc_log.h"
#include "drmutils.h"
#include "gralloctypes/Gralloc4.h"

#include "exynos_format.h"
#include "mali_gralloc_formats.h"

#include <pixel-gralloc/metadata.h>

#include <vector>

namespace arm
{
namespace mapper
{
namespace common
{

using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::StandardMetadataType;
using aidl::android::hardware::graphics::common::XyColor;
using aidl::android::hardware::graphics::common::Smpte2086;
using aidl::android::hardware::graphics::common::Cta861_3;
#if 0
using aidl::arm::graphics::ArmMetadataType;
#endif

using MetadataType = android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;

static int get_num_planes(const private_handle_t *hnd)
{
	if (is_exynos_format(hnd->get_alloc_format()))
	{
		switch (hnd->get_alloc_format())
		{
			case HAL_PIXEL_FORMAT_YCrCb_420_SP:
			case HAL_PIXEL_FORMAT_EXYNOS_YV12_M:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_P_M:
				return 3;

			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_TILED:
			case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M:
			case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_FULL:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_S10B:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_S10B:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_P010_M:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_M_10B_SBWC:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L50:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_SBWC_L75:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L50:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_SBWC_L75:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L40:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L60:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SP_M_10B_SBWC_L80:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L40:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L60:
			case HAL_PIXEL_FORMAT_EXYNOS_YCbCr_420_SPN_10B_SBWC_L80:
				return 2;
		}
	}

	return hnd->is_multi_plane() ? (hnd->plane_info[2].offset == 0 ? 2 : 3) : 1;
}

static std::vector<std::vector<PlaneLayoutComponent>> plane_layout_components_from_handle(const private_handle_t *hnd)
{
	/* Re-define the component constants to make the table easier to read. */
	const ExtendableType R = android::gralloc4::PlaneLayoutComponentType_R;
	const ExtendableType G = android::gralloc4::PlaneLayoutComponentType_G;
	const ExtendableType B = android::gralloc4::PlaneLayoutComponentType_B;
	const ExtendableType A = android::gralloc4::PlaneLayoutComponentType_A;
	const ExtendableType CB = android::gralloc4::PlaneLayoutComponentType_CB;
	const ExtendableType CR = android::gralloc4::PlaneLayoutComponentType_CR;
	const ExtendableType Y = android::gralloc4::PlaneLayoutComponentType_Y;
	const ExtendableType RAW = android::gralloc4::PlaneLayoutComponentType_RAW;

	struct table_entry
	{
		uint32_t drm_fourcc;
		std::vector<std::vector<PlaneLayoutComponent>> components;
	};

	/* clang-format off */
	static table_entry table[] = {
		/* 16 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_RGB565,
			.components = { { { B, 0, 5 }, { G, 5, 6 }, { R, 11, 5 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_BGR565,
			.components = { { { R, 0, 5 }, { G, 5, 6 }, { B, 11, 5 } } }
		},
		/* 24 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_BGR888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 } } }
		},
		/* 32 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_ARGB8888,
			.components = { { { B, 0, 8 }, { G, 8, 8 }, { R, 16, 8 }, { A, 24, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_ABGR8888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 }, { A, 24, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_XBGR8888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_ABGR2101010,
			.components = { { { R, 0, 10 }, { G, 10, 10 }, { B, 20, 10 }, { A, 30, 2 } } }
		},
		/* 64 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_ABGR16161616F,
			.components = { { { R, 0, 16 }, { G, 16, 16 }, { B, 32, 16 }, { A, 48, 16 } } }
		},
		/* Single plane 8 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_YUYV,
			.components = { { { Y, 0, 8 }, { CB, 8, 8 }, { Y, 16, 8 }, { CR, 24, 8 } } }
		},
		/* Single plane 10 bit YUV 4:4:4 */
		{
			.drm_fourcc = DRM_FORMAT_Y410,
			.components = { { { CB, 0, 10 }, { Y, 10, 10 }, { CR, 20, 10 }, { A, 30, 2 } } }
		},
		/* Single plane 10 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_Y210,
			.components = { { { Y, 6, 10 }, { CB, 22, 10 }, { Y, 38, 10 }, { CR, 54, 10 } } }
		},
		/* Single plane 10 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_Y0L2,
			.components = { {
				{ Y, 0, 10 }, { CB, 10, 10 }, { Y, 20, 10 }, { A, 30, 1 }, { A, 31, 1 },
				{ Y, 32, 10 }, { CR, 42, 10 }, { Y, 52, 10 }, { A, 62, 1 }, { A, 63, 1 }
			} }
		},
		/* Semi-planar 8 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_NV16,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 }, { CR, 8, 8 } }
			}
		},
		/* Semi-planar 8 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_NV12,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 }, { CR, 8, 8 } }
			}
		},
		{
			.drm_fourcc = DRM_FORMAT_NV21,
			.components = {
				{ { Y, 0, 8 } },
				{ { CR, 0, 8 }, { CB, 8, 8 } }
			}
		},
		/* Semi-planar 10 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_P210,
			.components = {
				{ { Y, 6, 10 } },
				{ { CB, 6, 10 }, { CB, 22, 10 } }
			}
		},
		/* Semi-planar 10 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_P010,
			.components = {
				{ { Y, 6, 10 } },
				{ { CB, 6, 10 }, { CR, 22, 10 } }
			}
		},
		/* Planar 8 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_YVU420,
			.components = {
				{ { Y, 0, 8 } },
				{ { CR, 0, 8 } },
				{ { CB, 0, 8 } }
			}
		},
		/* Planar 8 bit YUV 4:4:4 */
		{
			.drm_fourcc = DRM_FORMAT_YUV444,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 } },
				{ { CR, 0, 8 } }
			}
		},

		/* AFBC Only FourCC */
		{.drm_fourcc = DRM_FORMAT_YUV420_8BIT, .components = { {} } },
		{.drm_fourcc = DRM_FORMAT_YUV420_10BIT, .components = { {} } },

		/* Google specific formats */
		{
			.drm_fourcc = DRM_FORMAT_R8,
			.components = {
				{ { R, 0, 8 } }
			}
		},
		{
			.drm_fourcc = DRM_FORMAT_RG88,
			.components = {
				{ { R, 0, 8 }, { G, 8, 8 } }
			}
		},
	};
	/* clang-format on */

	const uint32_t drm_fourcc = drm_fourcc_from_handle(hnd);
	if (drm_fourcc != DRM_FORMAT_INVALID)
	{
		for (const auto& entry : table)
		{
			if (entry.drm_fourcc == drm_fourcc)
			{
				return entry.components;
			}
		}
	}

	switch (hnd->get_alloc_format())
	{
		case HAL_PIXEL_FORMAT_RAW10:
			return {{{RAW, 0, -1}}};
		case HAL_PIXEL_FORMAT_RAW12:
			return {{{RAW, 0, -1}}};
		case HAL_PIXEL_FORMAT_BLOB:
			break;
		default:
			MALI_GRALLOC_LOGW("Could not find component description for Format(%#x) FourCC value(%#x)",
				hnd->get_alloc_format(), drm_fourcc);
	}

	return std::vector<std::vector<PlaneLayoutComponent>>(0);
}

static android::status_t get_plane_layouts(const private_handle_t *handle, std::vector<PlaneLayout> *layouts)
{
	const int num_planes = get_num_planes(handle);
	uint32_t base_format = handle->alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK;
	int32_t format_index = get_format_index(base_format);
	if (format_index < 0)
	{
		MALI_GRALLOC_LOGE("Negative format index in get_plane_layouts");
		return android::BAD_VALUE;
	}
	const format_info_t format_info = formats[format_index];
	layouts->reserve(num_planes);

	if (is_exynos_format(handle->get_alloc_format()))
	{
		std::vector<std::vector<PlaneLayoutComponent>> components = plane_layout_components_from_handle(handle);
		for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
		{
			int64_t plane_size = handle->plane_info[plane_index].size;
			int64_t sample_increment_in_bits = format_info.bpp[plane_index];
			int64_t offset = handle->plane_info[plane_index].offset;

			static bool warn_multifd = true;
			if (warn_multifd) {
				uint8_t fd_count = get_exynos_fd_count(base_format);
				if (fd_count != 1) {
					warn_multifd = false;
					MALI_GRALLOC_LOGW("Offsets in plane layouts of multi-fd format (%s %" PRIu64
							") are not reliable. This can lead to image corruption.",
							format_name(base_format), handle->alloc_format);
				}
			}

			PlaneLayout layout = {.offsetInBytes = offset,
				                  .sampleIncrementInBits = sample_increment_in_bits,
				                  .strideInBytes = static_cast<int64_t>(handle->plane_info[plane_index].byte_stride),
				                  .widthInSamples = static_cast<int64_t>(handle->plane_info[plane_index].alloc_width),
				                  .heightInSamples = static_cast<int64_t>(handle->plane_info[plane_index].alloc_height),
				                  .totalSizeInBytes = plane_size,
				                  .horizontalSubsampling = (plane_index == 0 ? 1 : format_info.hsub),
				                  .verticalSubsampling = (plane_index == 0 ? 1 : format_info.vsub),
				                  .components = components.size() > plane_index ? components[plane_index] :
				                                                                  std::vector<PlaneLayoutComponent>(0) };
			layouts->push_back(layout);
		}
	}
	else
	{
		std::vector<std::vector<PlaneLayoutComponent>> components = plane_layout_components_from_handle(handle);
		for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
		{
			int64_t plane_size;
			if (plane_index < num_planes - 1)
			{
				plane_size = handle->plane_info[plane_index + 1].offset;
			}
			else
			{
				int64_t layer_size = handle->alloc_sizes[0] / handle->layer_count;
				plane_size = layer_size - handle->plane_info[plane_index].offset;
			}

			if (handle->fd_count > 1 && handle->plane_info[plane_index].fd_idx == plane_index)
			{
				plane_size = handle->plane_info[plane_index].size;
			}

			int64_t sample_increment_in_bits = 0;
			if (handle->alloc_format & MALI_GRALLOC_INTFMT_AFBC_BASIC)
			{
				sample_increment_in_bits = format_info.bpp_afbc[plane_index];
			}
			else
			{
				sample_increment_in_bits = format_info.bpp[plane_index];
			}

			switch (handle->get_alloc_format())
			{
				case HAL_PIXEL_FORMAT_RAW10:
				case HAL_PIXEL_FORMAT_RAW12:
					sample_increment_in_bits = 0;
					break;
				default:
					break;
			}

			PlaneLayout layout = {.offsetInBytes = handle->plane_info[plane_index].offset,
				                  .sampleIncrementInBits = sample_increment_in_bits,
				                  .strideInBytes = static_cast<int64_t>(handle->plane_info[plane_index].byte_stride),
				                  .widthInSamples = static_cast<int64_t>(handle->plane_info[plane_index].alloc_width),
				                  .heightInSamples = static_cast<int64_t>(handle->plane_info[plane_index].alloc_height),
				                  .totalSizeInBytes = plane_size,
				                  .horizontalSubsampling = (plane_index == 0 ? 1 : format_info.hsub),
				                  .verticalSubsampling = (plane_index == 0 ? 1 : format_info.vsub),
				                  .components = components.size() > plane_index ? components[plane_index] :
				                                                                  std::vector<PlaneLayoutComponent>(0) };
			layouts->push_back(layout);
		}
	}

	return android::OK;
}

static hidl_vec<uint8_t> encodePointer(void* ptr) {
	constexpr uint8_t kPtrSize = sizeof(void*);

	hidl_vec<uint8_t> output(kPtrSize);
	std::memcpy(output.data(), &ptr, kPtrSize);

	return output;
}

void get_metadata(const private_handle_t *handle, const IMapper::MetadataType &metadataType, IMapper::get_cb hidl_cb)
{
	android::status_t err = android::OK;
	hidl_vec<uint8_t> vec;

	if (android::gralloc4::isStandardMetadataType(metadataType))
	{
		switch (android::gralloc4::getStandardMetadataTypeValue(metadataType))
		{
		case StandardMetadataType::BUFFER_ID:
			err = android::gralloc4::encodeBufferId(handle->backing_store_id, &vec);
			break;
		case StandardMetadataType::NAME:
		{
			std::string name;
			get_name(handle, &name);
			err = android::gralloc4::encodeName(name, &vec);
			break;
		}
		case StandardMetadataType::WIDTH:
			err = android::gralloc4::encodeWidth(handle->width, &vec);
			break;
		case StandardMetadataType::HEIGHT:
			err = android::gralloc4::encodeHeight(handle->height, &vec);
			break;
		case StandardMetadataType::LAYER_COUNT:
			err = android::gralloc4::encodeLayerCount(handle->layer_count, &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
			err = android::gralloc4::encodePixelFormatRequested(static_cast<PixelFormat>(handle->req_format), &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
			err = android::gralloc4::encodePixelFormatFourCC(drm_fourcc_from_handle(handle), &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
			err = android::gralloc4::encodePixelFormatModifier(drm_modifier_from_handle(handle), &vec);
			break;
		case StandardMetadataType::USAGE:
			err = android::gralloc4::encodeUsage(handle->consumer_usage | handle->producer_usage, &vec);
			break;
		case StandardMetadataType::ALLOCATION_SIZE:
		{
			uint64_t total_size = 0;
			for (int fidx = 0; fidx < handle->fd_count; fidx++)
			{
				total_size += handle->alloc_sizes[fidx];
			}
			err = android::gralloc4::encodeAllocationSize(total_size, &vec);
			break;
		}
		case StandardMetadataType::PROTECTED_CONTENT:
		{
			/* This is set to 1 if the buffer has protected content. */
			const int is_protected =
			    (((handle->consumer_usage | handle->producer_usage) & BufferUsage::PROTECTED) == 0) ? 0 : 1;
			err = android::gralloc4::encodeProtectedContent(is_protected, &vec);
			break;
		}
		case StandardMetadataType::COMPRESSION:
		{
			ExtendableType compression;
			if (handle->alloc_format & MALI_GRALLOC_INTFMT_AFBC_BASIC)
			{
				compression = Compression_AFBC;
			}
			else
			{
				compression = android::gralloc4::Compression_None;
			}
			err = android::gralloc4::encodeCompression(compression, &vec);
			break;
		}
		case StandardMetadataType::INTERLACED:
			err = android::gralloc4::encodeInterlaced(android::gralloc4::Interlaced_None, &vec);
			break;
		case StandardMetadataType::CHROMA_SITING:
		{
			int format_index = get_format_index(handle->alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK);
			if (format_index < 0)
			{
				err = android::BAD_VALUE;
				break;
			}
			ExtendableType siting = android::gralloc4::ChromaSiting_None;
			if (formats[format_index].is_yuv)
			{
				siting = android::gralloc4::ChromaSiting_Unknown;
			}
			err = android::gralloc4::encodeChromaSiting(siting, &vec);
			break;
		}
		case StandardMetadataType::PLANE_LAYOUTS:
		{
			std::vector<PlaneLayout> layouts;
			err = get_plane_layouts(handle, &layouts);
			if (!err)
			{
				err = android::gralloc4::encodePlaneLayouts(layouts, &vec);
			}
			break;
		}
		case StandardMetadataType::DATASPACE:
		{
			std::optional<Dataspace> dataspace;
			get_dataspace(handle, &dataspace);
			err = android::gralloc4::encodeDataspace(dataspace.value_or(Dataspace::UNKNOWN), &vec);
			break;
		}
		case StandardMetadataType::BLEND_MODE:
		{
			std::optional<BlendMode> blend_mode;
			get_blend_mode(handle, &blend_mode);
			err = android::gralloc4::encodeBlendMode(blend_mode.value_or(BlendMode::INVALID), &vec);
			break;
		}
		case StandardMetadataType::CROP:
		{
			const int num_planes = get_num_planes(handle);
			std::vector<Rect> crops(num_planes);
			for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
			{
				/* Set the default crop rectangle. Android mandates that it must fit [0, 0, widthInSamples, heightInSamples]
				 * We always require using the requested width and height for the crop rectangle size.
				 * For planes > 0 the size might need to be scaled, but since we only use plane[0] for crop set it to the
				 * Android default of [0, 0, widthInSamples, heightInSamples] for other planes.
				 */
				Rect rect = {.top = 0,
				             .left = 0,
				             .right = static_cast<int32_t>(handle->plane_info[plane_index].alloc_width),
				             .bottom = static_cast<int32_t>(handle->plane_info[plane_index].alloc_height) };
				if (plane_index == 0)
				{
					std::optional<Rect> crop_rect;
					get_crop_rect(handle, &crop_rect);
					if (crop_rect.has_value())
					{
						rect = crop_rect.value();
					}
					else
					{
						rect = {.top = 0, .left = 0, .right = handle->width, .bottom = handle->height };
					}
				}
				crops[plane_index] = rect;
			}
			err = android::gralloc4::encodeCrop(crops, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			std::optional<Smpte2086> smpte2086;
			get_smpte2086(handle, &smpte2086);
			err = android::gralloc4::encodeSmpte2086(smpte2086, &vec);
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			std::optional<Cta861_3> cta861_3;
			get_cta861_3(handle, &cta861_3);
			err = android::gralloc4::encodeCta861_3(cta861_3, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			std::optional<std::vector<uint8_t>> smpte2094_40;
			get_smpte2094_40(handle, &smpte2094_40);
			err = android::gralloc4::encodeSmpte2094_40(smpte2094_40, &vec);
			break;
		}
		case StandardMetadataType::INVALID:
		default:
			err = android::BAD_VALUE;
		}
	}
	else if (metadataType.name == ::pixel::graphics::kPixelMetadataTypeName) {
		switch (static_cast<::pixel::graphics::MetadataType>(metadataType.value)) {
			case ::pixel::graphics::MetadataType::VIDEO_HDR:
				vec = encodePointer(get_video_hdr(handle));
				break;
			case ::pixel::graphics::MetadataType::VIDEO_ROI:
			{
				auto roi = get_video_roiinfo(handle);
				if (roi == nullptr) {
					err = android::BAD_VALUE;
				} else {
					vec = encodePointer(roi);
				}
				break;
			}
			case ::pixel::graphics::MetadataType::PLANE_DMA_BUFS:
			{
				std::vector<int> plane_fds(MAX_BUFFER_FDS, -1);
				for (int i = 0; i < get_num_planes(handle); i++) {
					plane_fds[i] = handle->fds[handle->plane_info[i].fd_idx];
				}
				vec = ::pixel::graphics::util::encodeVector<int>(plane_fds);
				break;
			}
			default:
				err = android::BAD_VALUE;
		}
	}
	else
	{
		err = android::BAD_VALUE;
	}

	hidl_cb((err) ? Error::UNSUPPORTED : Error::NONE, vec);
}

Error set_metadata(const private_handle_t *handle, const IMapper::MetadataType &metadataType,
                   const hidl_vec<uint8_t> &metadata)
{
	if (android::gralloc4::isStandardMetadataType(metadataType))
	{
		android::status_t err = android::OK;
		switch (android::gralloc4::getStandardMetadataTypeValue(metadataType))
		{
		case StandardMetadataType::DATASPACE:
		{
			Dataspace dataspace;
			err = android::gralloc4::decodeDataspace(metadata, &dataspace);
			if (!err)
			{
				set_dataspace(handle, dataspace);
			}
			break;
		}
		case StandardMetadataType::BLEND_MODE:
		{
			BlendMode blend_mode;
			err = android::gralloc4::decodeBlendMode(metadata, &blend_mode);
			if (!err)
			{
				set_blend_mode(handle, blend_mode);
			}
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			std::optional<Smpte2086> smpte2086;
			err = android::gralloc4::decodeSmpte2086(metadata, &smpte2086);
			if (!err)
			{
				err = set_smpte2086(handle, smpte2086);
			}
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			std::optional<Cta861_3> cta861_3;
			err = android::gralloc4::decodeCta861_3(metadata, &cta861_3);
			if (!err)
			{
				err = set_cta861_3(handle, cta861_3);
			}
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			std::optional<std::vector<uint8_t>> smpte2094_40;
			err = android::gralloc4::decodeSmpte2094_40(metadata, &smpte2094_40);
			if (!err)
			{
				err = set_smpte2094_40(handle, smpte2094_40);
			}
			break;
		}
		case StandardMetadataType::CROP:
		{
			std::vector<Rect> crops;
			err = android::gralloc4::decodeCrop(metadata, &crops);
			if (!err)
			{
				err = set_crop_rect(handle, crops[0]);
			}
			break;
		}
		/* The following meta data types cannot be changed after allocation. */
		case StandardMetadataType::BUFFER_ID:
		case StandardMetadataType::NAME:
		case StandardMetadataType::WIDTH:
		case StandardMetadataType::HEIGHT:
		case StandardMetadataType::LAYER_COUNT:
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
		case StandardMetadataType::USAGE:
			return Error::BAD_VALUE;
		/* Changing other metadata types is unsupported. */
		case StandardMetadataType::PLANE_LAYOUTS:
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
		case StandardMetadataType::ALLOCATION_SIZE:
		case StandardMetadataType::PROTECTED_CONTENT:
		case StandardMetadataType::COMPRESSION:
		case StandardMetadataType::INTERLACED:
		case StandardMetadataType::CHROMA_SITING:
		case StandardMetadataType::INVALID:
		default:
			return Error::UNSUPPORTED;
		}
		return ((err) ? Error::UNSUPPORTED : Error::NONE);
	}
	else
	{
		/* None of the vendor types support set. */
		return Error::UNSUPPORTED;
	}
}

void getFromBufferDescriptorInfo(IMapper::BufferDescriptorInfo const &description,
                                 IMapper::MetadataType const &metadataType,
                                 IMapper::getFromBufferDescriptorInfo_cb hidl_cb)
{
	/* This will hold the metadata that is returned. */
	hidl_vec<uint8_t> vec;

	buffer_descriptor_t descriptor;
	descriptor.width = description.width;
	descriptor.height = description.height;
	descriptor.layer_count = description.layerCount;
	descriptor.hal_format = static_cast<uint64_t>(description.format);
	descriptor.producer_usage = static_cast<uint64_t>(description.usage);
	descriptor.consumer_usage = descriptor.producer_usage;
	descriptor.format_type = MALI_GRALLOC_FORMAT_TYPE_USAGE;

	/* Check if it is possible to allocate a buffer for the given description */
	const int alloc_result = mali_gralloc_derive_format_and_size(&descriptor);
	if (alloc_result != 0)
	{
		MALI_GRALLOC_LOGV("Allocation for the given description will not succeed. error: %d", alloc_result);
		hidl_cb(Error::BAD_VALUE, vec);
		return;
	}
	/* Create buffer handle from the initialized descriptor without a backing store or shared metadata region.
	 * Used to share functionality with the normal metadata get function that can only use the allocated buffer handle
	 * and does not have the buffer descriptor available. */
	private_handle_t partial_handle(0, descriptor.alloc_sizes, descriptor.consumer_usage, descriptor.producer_usage,
			                nullptr, descriptor.fd_count,
	                                descriptor.hal_format, descriptor.alloc_format,
	                                descriptor.width, descriptor.height, descriptor.pixel_stride,
	                                descriptor.layer_count, descriptor.plane_info);
	if (android::gralloc4::isStandardMetadataType(metadataType))
	{
		android::status_t err = android::OK;

		switch (android::gralloc4::getStandardMetadataTypeValue(metadataType))
		{
		case StandardMetadataType::NAME:
			err = android::gralloc4::encodeName(description.name, &vec);
			break;
		case StandardMetadataType::WIDTH:
			err = android::gralloc4::encodeWidth(description.width, &vec);
			break;
		case StandardMetadataType::HEIGHT:
			err = android::gralloc4::encodeHeight(description.height, &vec);
			break;
		case StandardMetadataType::LAYER_COUNT:
			err = android::gralloc4::encodeLayerCount(description.layerCount, &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
			err = android::gralloc4::encodePixelFormatRequested(static_cast<PixelFormat>(description.format), &vec);
			break;
		case StandardMetadataType::USAGE:
			err = android::gralloc4::encodeUsage(description.usage, &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
			err = android::gralloc4::encodePixelFormatFourCC(drm_fourcc_from_handle(&partial_handle), &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
			err = android::gralloc4::encodePixelFormatModifier(drm_modifier_from_handle(&partial_handle), &vec);
			break;
		case StandardMetadataType::ALLOCATION_SIZE:
		{
			/* TODO: Returns expetected size if the buffer was actually allocated.
			 * Is this the right behavior?
			 */
			uint64_t total_size = 0;
			for (int fidx = 0; fidx < descriptor.fd_count; fidx++)
			{
				total_size += descriptor.alloc_sizes[fidx];
			}
			err = android::gralloc4::encodeAllocationSize(total_size, &vec);
			break;
		}
		case StandardMetadataType::PROTECTED_CONTENT:
		{
			/* This is set to 1 if the buffer has protected content. */
			const int is_protected =
			    (((partial_handle.consumer_usage | partial_handle.producer_usage) & BufferUsage::PROTECTED)) ? 1 : 0;
			err = android::gralloc4::encodeProtectedContent(is_protected, &vec);
			break;
		}
		case StandardMetadataType::COMPRESSION:
		{
			ExtendableType compression;
			if (partial_handle.alloc_format & MALI_GRALLOC_INTFMT_AFBC_BASIC)
			{
				compression = Compression_AFBC;
			}
			else
			{
				compression = android::gralloc4::Compression_None;
			}
			err = android::gralloc4::encodeCompression(compression, &vec);
			break;
		}
		case StandardMetadataType::INTERLACED:
			err = android::gralloc4::encodeInterlaced(android::gralloc4::Interlaced_None, &vec);
			break;
		case StandardMetadataType::CHROMA_SITING:
		{
			int format_index = get_format_index(partial_handle.alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK);
			if (format_index < 0)
			{
				err = android::BAD_VALUE;
				break;
			}
			ExtendableType siting = android::gralloc4::ChromaSiting_None;
			if (formats[format_index].is_yuv)
			{
				siting = android::gralloc4::ChromaSiting_Unknown;
			}
			err = android::gralloc4::encodeChromaSiting(siting, &vec);
			break;
		}
		case StandardMetadataType::PLANE_LAYOUTS:
		{
			std::vector<PlaneLayout> layouts;
			err = get_plane_layouts(&partial_handle, &layouts);
			if (!err)
			{
				err = android::gralloc4::encodePlaneLayouts(layouts, &vec);
			}
			break;
		}
		case StandardMetadataType::DATASPACE:
		{
			android_dataspace_t dataspace;
			get_format_dataspace(partial_handle.alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK,
			                     partial_handle.consumer_usage | partial_handle.producer_usage, partial_handle.width,
			                     partial_handle.height, &dataspace);
			err = android::gralloc4::encodeDataspace(static_cast<Dataspace>(dataspace), &vec);
			break;
		}
		case StandardMetadataType::BLEND_MODE:
			err = android::gralloc4::encodeBlendMode(BlendMode::INVALID, &vec);
			break;
		case StandardMetadataType::CROP:
		{
			const int num_planes = get_num_planes(&partial_handle);
			std::vector<Rect> crops(num_planes);
			for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
			{
				Rect rect = {.top = 0,
					         .left = 0,
					         .right = static_cast<int32_t>(partial_handle.plane_info[plane_index].alloc_width),
					         .bottom = static_cast<int32_t>(partial_handle.plane_info[plane_index].alloc_height) };
				if (plane_index == 0)
				{
					rect = {.top = 0, .left = 0, .right = partial_handle.width, .bottom = partial_handle.height };
				}
				crops[plane_index] = rect;
			}
			err = android::gralloc4::encodeCrop(crops, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			std::optional<Smpte2086> smpte2086{};
			err = android::gralloc4::encodeSmpte2086(smpte2086, &vec);
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			std::optional<Cta861_3> cta861_3{};
			err = android::gralloc4::encodeCta861_3(cta861_3, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			std::optional<std::vector<uint8_t>> smpte2094_40{};
			err = android::gralloc4::encodeSmpte2094_40(smpte2094_40, &vec);
			break;
		}

		case StandardMetadataType::BUFFER_ID:
		case StandardMetadataType::INVALID:
		default:
			err = android::BAD_VALUE;
		}
		hidl_cb((err) ? Error::UNSUPPORTED : Error::NONE, vec);
	}
	else
	{
		hidl_cb(Error::UNSUPPORTED, vec);
	}
}

} // namespace common
} // namespace mapper
} // namespace arm
