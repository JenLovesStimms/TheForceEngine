#include <cstring>

#include "rtexture.h"
#include <TFE_Game/igame.h>
#include <TFE_System/system.h>
#include <TFE_Archive/archive.h>
#include <TFE_Asset/assetSystem.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/Serialization/serialization.h>
#include <unordered_map>

using namespace TFE_DarkForces;
using namespace TFE_Memory;

namespace TFE_Jedi
{
	enum
	{
		DF_BM_VERSION = 30,
		DF_ANIM_ID = 2,
	};

	struct LevelTexture
	{
		std::string name;
		TextureData* texture;
	};
	typedef std::vector<LevelTexture> TextureList;
	typedef std::unordered_map<std::string, s32> TextureTable;
		
	struct TextureState
	{
		Allocator*    textureAnimAlloc = nullptr;
		Task*         textureAnimTask = nullptr;
		MemoryRegion* memoryRegion = nullptr;
		s32           animTexIndex = 0;
	};
	static TextureState s_texState = {};
	static std::vector<u8> s_buffer;
	static std::vector<TextureData*> s_tempTextureList;

	static TextureList  s_textureList[POOL_COUNT];
	static TextureTable s_textureTable[POOL_COUNT];

	void decompressColumn_Type1(const u8* src, u8* dst, s32 pixelCount);
	void decompressColumn_Type2(const u8* src, u8* dst, s32 pixelCount);
	void textureAnimationTaskFunc(MessageType msg);

	u8 readByte(const u8*& data)
	{
		u8 res = *data;
		data++;
		return res;
	}

	s16 readShort(const u8*& data)
	{
		s16 res = *((s16*)data);
		data += 2;
		return res;
	}

	u16 readUShort(const u8*& data)
	{
		u16 res = *((u16*)data);
		data += 2;
		return res;
	}

	s32 readInt(const u8*& data)
	{
		s32 res = *((s32*)data);
		data += 4;
		return res;
	}

	void bitmap_setupAnimationTask()
	{
		s_texState.textureAnimTask = createSubTask("texture animation", textureAnimationTaskFunc);
		s_texState.textureAnimAlloc = allocator_create(sizeof(AnimatedTexture));
		s_texState.animTexIndex = 0;
	}

	Allocator* bitmap_getAnimTextureAlloc()
	{
		return s_texState.textureAnimAlloc;
	}

	MemoryRegion* bitmap_getAllocator()
	{
		return s_texState.memoryRegion;
	}

	void bitmap_setAllocator(MemoryRegion* allocator)
	{
		s_texState.memoryRegion = allocator;
	}
		
	// Added for TFE to clear out per-level texture data.
	void bitmap_clearLevelData()
	{
		s_textureList[POOL_LEVEL].clear();
		s_textureTable[POOL_LEVEL].clear();
	}

	void bitmap_clearAll()
	{
		s_texState = {};
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			s_textureList[p].clear();
			s_textureTable[p].clear();
		}
	}

	bool bitmap_getTextureIndex(TextureData* tex, s32* index, AssetPool* pool)
	{
		for (s32 p = 0; p < POOL_COUNT; p++)
		{
			s32 count = (s32)s_textureList[p].size();
			const LevelTexture* srcList = s_textureList[p].data();
			for (s32 i = 0; i < count; i++)
			{
				if (srcList[i].texture == tex)
				{
					*index = i;
					*pool = AssetPool(p);
					return true;
				}
			}
		}
		return false;
	}

	TextureData* bitmap_getTextureByIndex(s32 index, AssetPool pool)
	{
		return s_textureList[pool][index].texture;
	}

	// Serialize only level textures.
	void bitmap_serializeLevelTextures(Stream* stream)
	{
		s32 count = 0;
		LevelTexture* list = nullptr;
		if (serialization_getMode() == SMODE_WRITE)
		{
			count = (s32)s_textureList[POOL_LEVEL].size();
		}
		SERIALIZE(SaveVersionInit, count, 0);
		if (serialization_getMode() == SMODE_READ)
		{
			s_textureList[POOL_LEVEL].resize(count);
			s_textureTable[POOL_LEVEL].clear();
		}
		list = s_textureList[POOL_LEVEL].data();

		for (s32 i = 0; i < count; i++, list++)
		{
			// Assume names are less than 256 characters.
			u8 length = 0;
			if (serialization_getMode() == SMODE_WRITE)
			{
				length = (u8)list->name.length();
			}
			SERIALIZE(SaveVersionInit, length, 0);
			if (serialization_getMode() == SMODE_READ)
			{
				list->name.resize(length);
			}
			SERIALIZE_BUF(SaveVersionInit, &list->name[0], length);

			// If reading, we need to load the texture now.
			if (serialization_getMode() == SMODE_READ)
			{
				const char* name = list->name.c_str();
				list->texture = bitmap_load(name, 1, POOL_LEVEL, false);
				s_textureTable[POOL_LEVEL][name] = i;
			}
		}
	}
		
	TextureData** bitmap_getTextures(s32* textureCount, AssetPool pool)
	{
		assert(textureCount);
		s32 count = (s32)s_textureList[pool].size();
		s_tempTextureList.resize(count);
		TextureData** list = s_tempTextureList.data();
		const LevelTexture* srcList = s_textureList[pool].data();
		for (s32 i = 0; i < count; i++)
		{
			list[i] = srcList[i].texture;
		}
		*textureCount = count;
		return list;
	}

	TextureData* bitmap_load(const char* name, u32 decompress, AssetPool pool, bool addToCache)
	{
		// TFE: Keep track of per-level texture state for serialization.
		// This is also useful for handling per-level GPU texture mirrors.
		TextureTable::iterator iTex = s_textureTable[pool].find(name);
		if (iTex != s_textureTable[pool].end())
		{
			return s_textureList[pool][iTex->second].texture;
		}

		FilePath filepath;
		if (!TFE_Paths::getFilePath(name, &filepath))
		{
			return nullptr;
		}

		FileStream file;
		if (!file.open(&filepath, FileStream::MODE_READ))
		{
			return nullptr;
		}

		size_t size = file.getSize();
		s_buffer.resize(size);
		file.readBuffer(s_buffer.data(), (u32)size);
		file.close();

		TextureData* texture = (TextureData*)region_alloc(s_texState.memoryRegion, sizeof(TextureData));
		const u8* data = s_buffer.data();
		const u8* fheader = data;
		data += 3;

		if (strncmp((char*)fheader, "BM ", 3))
		{
			TFE_System::logWrite(LOG_ERROR, "bitmap_load", "File '%s' is not a valid BM file.", filepath);
			return nullptr;
		}

		u8 version = readByte(data);
		if (version != DF_BM_VERSION)
		{
			TFE_System::logWrite(LOG_ERROR, "bitmap_load", "File '%s' has invalid BM version '%u'.", filepath, version);
			return nullptr;
		}

		texture->width = readUShort(data);
		texture->height = readUShort(data);
		texture->uvWidth = readShort(data);
		texture->uvHeight = readShort(data);
		texture->flags = readByte(data);
		texture->logSizeY = readByte(data);
		texture->compressed = readByte(data);
		// value is ignored.
		data++;
		
		if (texture->compressed)
		{
			s32 inSize = readInt(data);
			// values are ignored.
			data += 12;

			if (decompress & 1)
			{
				texture->dataSize = texture->width * texture->height;
				texture->image = (u8*)region_alloc(s_texState.memoryRegion, texture->dataSize);

				const u8* inBuffer = data;
				data += inSize;

				const u32* columns = (u32*)data;
				data += sizeof(u32) * texture->width;

				if (texture->compressed == 1)
				{
					u8* dst = texture->image;
					for (s32 i = 0; i < texture->width; i++, dst += texture->height)
					{
						const u8* src = &inBuffer[columns[i]];
						decompressColumn_Type1(src, dst, texture->height);
					}
				}
				else if (texture->compressed == 2)
				{
					u8* dst = texture->image;
					for (s32 i = 0; i < texture->width; i++, dst += texture->height)
					{
						const u8* src = &inBuffer[columns[i]];
						decompressColumn_Type2(src, dst, texture->height);
					}
				}
				texture->compressed = 0;
				texture->columns = nullptr;
			}
			else
			{
				texture->dataSize = inSize;
				texture->image = (u8*)region_alloc(s_texState.memoryRegion, texture->dataSize);
				memcpy(texture->image, data, texture->dataSize);
				data += texture->dataSize;

				texture->columns = (u32*)region_alloc(s_texState.memoryRegion, texture->width * sizeof(u32));
				memcpy(texture->columns, data, texture->width * sizeof(u32));
				data += texture->width * sizeof(u32);
			}
		}
		else
		{
			texture->dataSize = texture->width * texture->height;
			// Datasize, ignored.
			data += 4;
			texture->columns = nullptr;

			// Padding, ignored.
			data += 12;

			// Allocate and read the BM image.
			texture->image = (u8*)region_alloc(s_texState.memoryRegion, texture->dataSize);
			memcpy(texture->image, data, texture->dataSize);
			data += texture->dataSize;
		}

		// Add the texture to the level texture cache if appropriate.
		if (addToCache)
		{
			s32 index = (s32)s_textureList[pool].size();
			s_textureList[pool].push_back({ name, texture });
			s_textureTable[pool][name] = index;
		}

		texture->animIndex = -1;
		texture->frameIdx = -1;
		texture->animPtr = nullptr;

		return texture;
	}

	TextureData* bitmap_loadFromMemory(const u8* data, size_t size, u32 decompress)
	{
		TextureData* texture = (TextureData*)malloc(sizeof(TextureData));
		const u8* fheader = data;
		data += 3;

		if (strncmp((char*)fheader, "BM ", 3))
		{
			TFE_System::logWrite(LOG_ERROR, "bitmap_load", "Load From Memory - invalid data.");
			return nullptr;
		}

		u8 version = readByte(data);
		if (version != DF_BM_VERSION)
		{
			TFE_System::logWrite(LOG_ERROR, "bitmap_load", "Load From Memory - invalid BM version '%u'.", version);
			return nullptr;
		}

		texture->width = readUShort(data);
		texture->height = readUShort(data);
		texture->uvWidth = readShort(data);
		texture->uvHeight = readShort(data);
		texture->flags = readByte(data);
		texture->logSizeY = readByte(data);
		texture->compressed = readByte(data);
		// value is ignored.
		data++;

		if (texture->compressed)
		{
			s32 inSize = readInt(data);
			// values are ignored.
			data += 12;

			if (decompress & 1)
			{
				texture->dataSize = texture->width * texture->height;
				texture->image = (u8*)malloc(texture->dataSize);

				const u8* inBuffer = data;
				data += inSize;

				const u32* columns = (u32*)data;
				data += sizeof(u32) * texture->width;

				if (texture->compressed == 1)
				{
					u8* dst = texture->image;
					for (s32 i = 0; i < texture->width; i++, dst += texture->height)
					{
						const u8* src = &inBuffer[columns[i]];
						decompressColumn_Type1(src, dst, texture->height);
					}
				}
				else if (texture->compressed == 2)
				{
					u8* dst = texture->image;
					for (s32 i = 0; i < texture->width; i++, dst += texture->height)
					{
						const u8* src = &inBuffer[columns[i]];
						decompressColumn_Type2(src, dst, texture->height);
					}
				}
				texture->compressed = 0;
				texture->columns = nullptr;
			}
			else
			{
				texture->dataSize = inSize;
				texture->image = (u8*)malloc(texture->dataSize);
				memcpy(texture->image, data, texture->dataSize);
				data += texture->dataSize;

				texture->columns = (u32*)malloc(texture->width * sizeof(u32));
				if (texture->columns)
				{
					memcpy(texture->columns, data, texture->width * sizeof(u32));
				}
				data += texture->width * sizeof(u32);
			}
		}
		else
		{
			texture->dataSize = texture->width * texture->height;
			// Datasize, ignored.
			data += 4;
			texture->columns = nullptr;

			// Padding, ignored.
			data += 12;

			// Allocate and read the BM image.
			texture->image = (u8*)malloc(texture->dataSize);
			if (texture->image)
			{
				memcpy(texture->image, data, texture->dataSize);
			}
			data += texture->dataSize;
		}

		return texture;
	}

	Allocator* bitmap_getAnimatedTextures()
	{
		return s_texState.textureAnimAlloc;
	}

	AnimatedTexture* bitmap_createAnimatedTexture(TextureData** texture, s32 index, u8& frameRate)
	{
		TextureData* tex = *texture;
		frameRate = tex->image[0];
		u8 animatedId = tex->image[1];

		// if animatedId != DF_ANIM_ID, then this is not a properly setup animated texture.
		if (animatedId != DF_ANIM_ID)
		{
			TFE_System::logWrite(LOG_WARNING, "bitmap_setupAnimatedTexture", "Invalid animatedId %u, should be %u.", animatedId, DF_ANIM_ID);
			tex->uvWidth = 0;
			return nullptr;
		}

		// In the original DOS code, this is directly set to pointers. But since TFE is compiled as 64-bit, pointers are not the correct size.
		u32* textureOffsets = (u32*)(tex->image + 2);
		AnimatedTexture* anim = (AnimatedTexture*)allocator_newItem(s_texState.textureAnimAlloc);

		// 64 bit pointers are larger than the offsets, so we have to allocate more space (for now).
		anim->frame = 0;
		anim->count = tex->uvHeight;	// frame count is packed into uvHeight.
		anim->texPtr = texture;			// pointer to the texture pointer, allowing us to update that pointer later.
		anim->baseFrame = tex;
		anim->baseData = tex->image;
		anim->frameList = (TextureData**)level_alloc(sizeof(TextureData**) * anim->count);
		// Allocate frame memory here since load-in-place does not work because structure size changes.
		TextureData* outFrames = (TextureData*)level_alloc(sizeof(TextureData) * anim->count);
		assert(anim->frameList);

		const u8* base = tex->image + 2;
		for (s32 i = 0; i < anim->count; i++)
		{
			const TextureData* frame = (TextureData*)(base + textureOffsets[i]);
			outFrames[i] = *frame;

			// Somehow this doesn't crash in DOS...
			if (frame->width >= 32768 || frame->height >= 32768)
			{
				outFrames[i] = outFrames[0];
			}

			// Allocate an image buffer since everything no longer fits nicely.
			outFrames[i].image = (u8*)level_alloc(outFrames[i].width * outFrames[i].height);
			memcpy(outFrames[i].image, (u8*)frame + 0x1c, outFrames[i].width * outFrames[i].height);

			// We have to make sure the structure offsets line up with DOS...
			outFrames[i].flags = *((u8*)frame + 0x18);
			outFrames[i].compressed = *((u8*)frame + 0x19);
			outFrames[i].animIndex = index;
			outFrames[i].frameIdx = i;
			outFrames[i].animPtr = anim;

			anim->frameList[i] = &outFrames[i];
		}
		return anim;
	}

	bool bitmap_setupAnimatedTexture(TextureData** texture, s32 index)
	{
		u8 frameRate;
		AnimatedTexture* anim = bitmap_createAnimatedTexture(texture, index, frameRate);
		if (!anim) { return false; }

		TextureData* tex = *texture;

		if (frameRate)
		{
			anim->delay = time_frameRateToDelay(frameRate);	// Delay is in "ticks."
			anim->nextTick = 0;
			*texture = anim->frameList[0];
		}
		else
		{
			// Hold indefinitely.
			anim->nextTick = 0xffffffff;
			// The "image" is really the animation.
			tex->image = (u8*)anim;
			tex->animIndex = index;
			tex->frameIdx = -1;
			tex->animPtr = nullptr; // This is set to NULL since it is handled in the INF serialization.
		}
		s_texState.animTexIndex++;
		return true;
	}
		
	// Per frame animated texture update.
	void textureAnimationTaskFunc(MessageType msg)
	{
		task_begin;
		while (msg != MSG_FREE_TASK)
		{
			// No persistent state is required.
			{
				AnimatedTexture* animTex = (AnimatedTexture*)allocator_getHead(s_texState.textureAnimAlloc);
				while (animTex)
				{
					if (animTex->nextTick < s_curTick)
					{
						if (animTex->nextTick == 0)
						{
							animTex->nextTick = s_curTick;
						}

						animTex->frame++;
						if (animTex->frame >= animTex->count)
						{
							animTex->frame = 0;
						}

						*animTex->texPtr = animTex->frameList[animTex->frame];
						animTex->nextTick += animTex->delay;
					}
					animTex = (AnimatedTexture*)allocator_getNext(s_texState.textureAnimAlloc);
				}
			}
			task_yield(TASK_NO_DELAY);
		}
		task_end;
	}
		
	// Type 1: RLE with runs of solid colors. Costs 2 bytes per solid-color run.
	void decompressColumn_Type1(const u8* src, u8* dst, s32 pixelCount)
	{
		while (pixelCount)
		{
			const u8 value = *src;
			src++;

			if (value & 0x80)
			{
				const u8 color = *src;
				src++;

				const u32 count = value & 0x7f;
				// store packed at dst
				for (u32 i = 0; i < count; i++, dst++)
				{
					*dst = color;
				}
				pixelCount -= count;
			}
			else
			{
				const u32 count = value;
				// store packed at dst
				for (u32 i = 0; i < count; i++, dst++, src++)
				{
					*dst = *src;
				}
				pixelCount -= count;
			}
		}
	}

	// Type 2: RLE with runs of transparent texels (useful for sprites). Costs 1 byte per transparent run.
	void decompressColumn_Type2(const u8* src, u8* dst, s32 pixelCount)
	{
		while (pixelCount)
		{
			const u8 value = *src;
			src++;

			if (value & 0x80)
			{
				const u32 count = value & 0x7f;
				// store 0 at dst
				memset(dst, 0, count);
				dst += count;
				pixelCount -= count;
			}
			else
			{
				const u32 count = value;
				// store packed at dst
				for (u32 i = 0; i < count; i++, dst++, src++)
				{
					*dst = *src;
				}
				pixelCount -= count;
			}
		}
	}
}