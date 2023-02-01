#pragma once
//////////////////////////////////////////////////////////////////////
// Pack level textures into an atlas texture or array of textures.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_System/memoryPool.h>
#include <TFE_Jedi/Renderer/textureInfo.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_RenderBackend/textureGpu.h>
#include <TFE_RenderBackend/shaderBuffer.h>


struct TextureData;
struct AnimatedTexture;
struct WaxFrame;

namespace TFE_Jedi
{
	struct TextureNode;

	enum
	{
		TexturePackerFlags_None = 0,
		TexturePackerFlags_TrueColor = FLAG_BIT(0),
	};

	struct TexturePage
	{
		s32 textureCount;
		u8* backingMemory = nullptr;
		TextureNode* root = nullptr;
	};

	struct TexturePacker
	{
		// GPU resources
		ShaderBuffer textureTableGPU;	// full texture table, includes all pages.
		TextureGpu* texture = nullptr;	// texture array, where each slice is a page.

		// CPU memory, this is kept around so it can be used on multiple levels.
		Vec4i* textureTable = nullptr;	// CPU memory for texture table.
		TexturePage** pages = nullptr;	// CPU copy of texture pages.

		// General Data
		u32 flags = TexturePackerFlags_None;
		s32 width = 0;					// Page dimensions.
		s32 height = 0;
		s32 texturesPacked = 0;			// Total textures packed over all pages.
		s32 pageCount = 0;				// Number of texture pages.
		s32 reservedPages = 0;			// Number of reserved pages.
		s32 reservedTexturesPacked = 0;

		// For debugging.
		char name[64];
	};

	// Initialize the texture packer once, it is persistent across levels.
	TexturePacker* texturepacker_init(const char* name, s32 width, s32 height, u32 flags/*TexturePackerFlags_*/);
	// Free memory and GPU buffers. Note: GPU textures need to be persistent, so the level allocator will not be used.
	void texturepacker_destroy(TexturePacker* texturePacker);

	// Begin the packing process, this clears out the texture packer.
	bool texturepacker_begin(TexturePacker* texturePacker);
	// Commit the final packing to GPU memory.
	void texturepacker_commit();

	// Reset the texture packer for new games.
	void texturepacker_reset();

	// Reserved Pages
	void texturepacker_reserveCommitedPages(TexturePacker* texturePacker);
	bool texturepacker_hasReservedPages(TexturePacker* texturePacker);
	void texturepacker_discardUnreservedPages(TexturePacker* texturePacker);

	// Global Texture Packer.
	TexturePacker* texturepacker_getGlobal();
	void texturepacker_freeGlobal();

	// Pack textures of various types into a single texture atlas.
	// The client must provide a 'getList' function to get a list of 'TextureInfo' (see above).
	// Note this may be called multiple times on the same texture packer, new pages are created as needed.
	s32 texturepacker_pack(TextureListCallback getList, AssetPool pool);
}  // TFE_Jedi