/*
 * Texture garbage collection policies:
 *
 * Default: When DelayedDelete is off, delete unused textures immediately.
 *          When on, only delete textures when we change themes, on
 * DoDelayedDelete().
 *
 * Volatile: Delete unused textures once they've been used at least once. Ignore
 *           DelayedDelete.
 *
 *           This is for banners.  We don't want to load all low-quality banners
 * in memory at once, since it might be ten megs of textures, and we don't need
 * to, since we can reload them very quickly.  We don't want to keep
 *           high quality textures in memory, either, although it's unlikely
 * that a player could actually view all banners long enough to transition to
 * them all in the course of one song select screen.
 *
 * If a texture is loaded as DEFAULT that was already loaded as VOLATILE,
 * DEFAULT overrides.
 */

#include "Etterna/Globals/global.h"
#include "Etterna/Actor/Base/ActorUtil.h"
#include "Etterna/Singletons/PrefsManager.h"
#include "RageBitmapTexture.h"
#include "RageDisplay.h"
#include "Core/Services/Locator.hpp"
#include "RageTextureManager.h"
#include "RageUtil/Utils/RageUtil.h"
#include "RageUtil/Misc/RageThreads.h"
#include "Etterna/Screen/Others/Screen.h"
#include "Etterna/Singletons/ScreenManager.h"
#include "arch/MovieTexture/MovieTexture.h"
#include "Etterna/Singletons/ScoreManager.h"

#include <map>
#include <algorithm>

RageTextureManager* TEXTUREMAN =
  nullptr; // global and accessible from anywhere in our program

namespace {
RageSharedMutex pathToTextureLock("TextureManager PathToTexture Lock");
std::map<RageTextureID, RageTexture*> m_mapPathToTexture;
RageSharedMutex texturesToUpdateLock("TextureManager texturesToUpdate Lock");
std::map<RageTextureID, RageTexture*> m_textures_to_update;
RageSharedMutex textureIdsByPointerLock(
  "TextureManager textureIdsByPointer Lock");
std::map<RageTexture*, RageTextureID> m_texture_ids_by_pointer;
} // namespace;

RageTextureManager::RageTextureManager()
{
	// Register with Lua.
	{
		Lua* L = LUA->Get();
		lua_pushstring(L, "TEXTUREMAN");
		this->PushSelf(L);
		lua_settable(L, LUA_GLOBALSINDEX);
		LUA->Release(L);
	}
}

RageTextureManager::~RageTextureManager()
{
	pathToTextureLock.Lock();
	texturesToUpdateLock.Lock();
	textureIdsByPointerLock.Lock();

	for (auto& i : m_mapPathToTexture) {
		auto pTexture = i.second;
		if (pTexture->m_iRefCount)
			Locator::getLogger()->warn("TEXTUREMAN LEAK: '{}', RefCount = {}.",
									   i.first.filename.c_str(),
									   pTexture->m_iRefCount);
		SAFE_DELETE(pTexture);
	}
	m_textures_to_update.clear();
	m_texture_ids_by_pointer.clear();

	pathToTextureLock.Unlock();
	texturesToUpdateLock.Unlock();
	textureIdsByPointerLock.Unlock();
}

void
RageTextureManager::Update(float fDeltaTime)
{
	static RageTimer garbageCollector;
	if (garbageCollector.PeekDeltaTime() >= 30.0f) {
		if ((SCREENMAN != nullptr) && (SCREENMAN->GetTopScreen() != nullptr) &&
			SCREENMAN->GetTopScreen()->GetScreenType() != gameplay) {
			DoDelayedDelete();
			garbageCollector.Touch();
		}
	}

	texturesToUpdateLock.Lock();
	for (auto& id : m_textures_to_update) {
		auto pTexture = id.second;
		pTexture->Update(fDeltaTime);
	}
	texturesToUpdateLock.Unlock();
}

void
RageTextureManager::AdjustTextureID(RageTextureID& ID) const
{
	if (ID.iColorDepth == -1)
		ID.iColorDepth = m_Prefs.m_iTextureColorDepth;
	ID.iMaxSize = std::min(ID.iMaxSize, m_Prefs.m_iMaxTextureResolution);
	if (m_Prefs.m_bMipMaps)
		ID.bMipMaps = true;
}

bool
RageTextureManager::IsTextureRegistered(RageTextureID ID) const
{
	AdjustTextureID(ID);
	pathToTextureLock.LockShared();
	bool ret = m_mapPathToTexture.find(ID) != m_mapPathToTexture.end();
	pathToTextureLock.UnlockShared();
	return ret;
}

/* If you've set up a texture yourself, register it here so it can be referenced
 * and deleted by ID.  This takes ownership; the texture will be freed according
 * to its GC policy. */
void
RageTextureManager::RegisterTexture(RageTextureID ID, RageTexture* pTexture)
{
	AdjustTextureID(ID);

	pathToTextureLock.Lock();
	textureIdsByPointerLock.Lock();

	/* Make sure we don't already have a texture with this ID.  If we do, the
	 * caller should have used it. */
	if (m_mapPathToTexture.count(ID) == 1) {
		/* Oops, found the texture. */
		RageException::Throw("Custom texture \"%s\" already registered!",
							 ID.filename.c_str());
	}

	m_mapPathToTexture[ID] = pTexture;
	m_texture_ids_by_pointer[pTexture] = ID;

	pathToTextureLock.Unlock();
	textureIdsByPointerLock.Unlock();
}

void
RageTextureManager::RegisterTextureForUpdating(const RageTextureID& id,
											   RageTexture* tex)
{
	texturesToUpdateLock.Lock();
	m_textures_to_update[id] = tex;
	texturesToUpdateLock.Unlock();
}

static const std::string g_sDefaultTextureName = "__blank__";
RageTextureID
RageTextureManager::GetDefaultTextureID()
{
	return RageTextureID(g_sDefaultTextureName);
}

static const std::string g_ScreenTextureName = "__screen__";
RageTextureID
RageTextureManager::GetScreenTextureID()
{
	return RageTextureID(g_ScreenTextureName);
}

RageSurface*
RageTextureManager::GetScreenSurface()
{
	return DISPLAY->CreateScreenshot();
}

class RageTexture_Default : public RageTexture
{
  public:
	RageTexture_Default()
	  : RageTexture(RageTextureID())
	{
		m_iSourceWidth = m_iSourceHeight = 1;
		m_iTextureWidth = m_iTextureHeight = 1;
		m_iImageWidth = m_iImageHeight = 1;
		CreateFrameRects();
	}
	intptr_t GetTexHandle() const override { return m_uTexHandle; }

  private:
	intptr_t m_uTexHandle{ 0 };
};

// Load and unload textures from disk.
RageTexture*
RageTextureManager::LoadTextureInternal(RageTextureID ID)
{
	Locator::getLogger()->trace("RageTextureManager::LoadTexture({}).",
								ID.filename.c_str());

	AdjustTextureID(ID);

	pathToTextureLock.Lock();

	/* We could have two copies of the same bitmap if there are equivalent but
	 * different paths, e.g. "Bitmaps\me.bmp" and "..\Rage PC
	 * Edition\Bitmaps\me.bmp". */
	const auto p = m_mapPathToTexture.find(ID);
	if (p != m_mapPathToTexture.end()) {
		/* Found the texture.  Just increase the refcount and return it. */
		const auto pTexture = p->second;
		pTexture->m_iRefCount++;

		pathToTextureLock.Unlock();
		return pTexture;
	}
	pathToTextureLock.Unlock();
	textureIdsByPointerLock.Unlock();

	// The texture is not already loaded.  Load it.

	RageTexture* pTexture;
	if (ID.filename == g_sDefaultTextureName) {
		pTexture = new RageTexture_Default;
	} else if (ActorUtil::GetFileType(ID.filename) == FT_Movie) {
		pTexture = RageMovieTexture::Create(ID);
	} else {
		pTexture = new RageBitmapTexture(ID);
	}
	pathToTextureLock.Lock();
	textureIdsByPointerLock.Lock();

	m_mapPathToTexture[ID] = pTexture;
	m_texture_ids_by_pointer[pTexture] = ID;

	pathToTextureLock.Unlock();
	textureIdsByPointerLock.Unlock();

	return pTexture;
}

/* Load a normal texture.  Use this call to actually use a texture. */
RageTexture*
RageTextureManager::LoadTexture(const RageTextureID& ID)
{
	auto pTexture = LoadTextureInternal(ID);
	if (pTexture != nullptr) {
		pTexture->m_lastRefTime.Touch();
		pTexture->m_bWasUsed = true;
	}

	return pTexture;
}

/* Asynchronously begin loading in texture for later use*/
void
RageTextureManager::AsyncLoadTexture(RageTextureID ID)
{
	RageThread thread;
	thread.SetName("Async Texture Loading");
	thread.Create(AsyncLoadTextureInternal, &ID);
}

int
AsyncLoadTextureInternal(void* data)
{
	// TODO: A lot of the code in here is duplicated from LoadTextureInternal
	// and probably shouldn't be
	RageTextureID ID = *reinterpret_cast<RageTextureID*>(data);
	pathToTextureLock.LockShared();
	const auto ret = (m_mapPathToTexture.find(ID) != m_mapPathToTexture.end());
	pathToTextureLock.UnlockShared();
	if (ret)
		return 0; // Texture is already loaded, no need to async load it

	RageTexture* pTexture;
	if (ID.filename == g_sDefaultTextureName) {
		pTexture = new RageTexture_Default;
	} else if (ActorUtil::GetFileType(ID.filename) == FT_Movie) {
		pTexture = RageMovieTexture::Create(ID);
	} else {
		pTexture = new RageBitmapTexture(ID);
	}
	pathToTextureLock.Lock();
	textureIdsByPointerLock.Lock();

	m_mapPathToTexture[ID] = pTexture;
	m_texture_ids_by_pointer[pTexture] = ID;

	pathToTextureLock.Unlock();
	textureIdsByPointerLock.Unlock();
	return 0;
}

RageTexture*
RageTextureManager::CopyTexture(RageTexture* pCopy)
{
	++pCopy->m_iRefCount;
	return pCopy;
}

void
RageTextureManager::VolatileTexture(const RageTextureID& ID)
{
	auto pTexture = LoadTextureInternal(ID);
	pTexture->GetPolicy() =
	  std::min(pTexture->GetPolicy(), RageTextureID::TEX_VOLATILE);
	UnloadTexture(pTexture);
}

void
RageTextureManager::UnloadTexture(RageTexture* t)
{
	if (t == nullptr)
		return;

	t->m_iRefCount--;
	ASSERT_M(t->m_iRefCount >= 0,
			 ssprintf("%i, %s", t->m_iRefCount, t->GetID().filename.c_str()));

	if (t->m_iRefCount != 0)
		return; /* Can't unload textures that are still referenced. */

	auto bDeleteThis = false;

	/* Always unload movies, so we don't waste time decoding. */
	if (t->IsAMovie())
		bDeleteThis = true;

	/* Delete normal textures immediately unless m_bDelayedDelete is is on. */
	if (t->GetPolicy() == RageTextureID::TEX_DEFAULT &&
		(!m_Prefs.m_bDelayedDelete ||
		 t->m_lastRefTime.PeekDeltaTime() >= 30.0f))
		bDeleteThis = true;

	/* Delete volatile textures after they've been used at least once. */
	if (t->GetPolicy() == RageTextureID::TEX_VOLATILE && t->m_bWasUsed)
		bDeleteThis = true;

	if (bDeleteThis)
		DeleteTexture(t);
}

void
RageTextureManager::DeleteTexture(RageTexture* t)
{
	ASSERT(t->m_iRefCount == 0);
	// LOG->Trace( "RageTextureManager: deleting '%s'.",
	// t->GetID().filename.c_str() );

	textureIdsByPointerLock.Lock();
	pathToTextureLock.Lock();
	texturesToUpdateLock.Lock();

	const auto id_entry = m_texture_ids_by_pointer.find(t);
	if (id_entry != m_texture_ids_by_pointer.end()) {
		const auto tex_entry = m_mapPathToTexture.find(id_entry->second);
		if (tex_entry != m_mapPathToTexture.end()) {
			m_mapPathToTexture.erase(tex_entry);
			SAFE_DELETE(t);
		}
		const auto tex_update_entry =
		  m_textures_to_update.find(id_entry->second);
		if (tex_update_entry != m_textures_to_update.end()) {
			m_textures_to_update.erase(tex_update_entry);
		}
		m_texture_ids_by_pointer.erase(id_entry);

		textureIdsByPointerLock.Unlock();
		pathToTextureLock.Unlock();
		texturesToUpdateLock.Unlock();
		return;
	}

	textureIdsByPointerLock.Unlock();
	pathToTextureLock.Unlock();
	texturesToUpdateLock.Unlock();
	FAIL_M("Tried to delete a texture that wasn't in the ids by pointer list.");
}

void
RageTextureManager::GarbageCollect(GCType type)
{
	// Search for old textures with refcount==0 to unload
	Locator::getLogger()->debug("Performing texture garbage collection.");

	pathToTextureLock.Lock();

	for (auto i = m_mapPathToTexture.begin(); i != m_mapPathToTexture.end();) {
		const auto j = i;
		i++;

		auto sPath = j->first.filename;
		auto t = j->second;

		if (t->m_iRefCount)
			continue; /* Can't unload textures that are still referenced. */

		auto bDeleteThis = false;
		if (type == screen_changed) {
			const auto policy = t->GetPolicy();
			switch (policy) {
				case RageTextureID::TEX_DEFAULT:
					/* If m_bDelayedDelete, wait until delayed_delete.  If
					 * !m_bDelayedDelete, it should have been deleted when it
					 * reached no references, but we might have just changed the
					 * preference. */
					if (!m_Prefs.m_bDelayedDelete)
						bDeleteThis = true;
					break;
				case RageTextureID::TEX_VOLATILE:
					bDeleteThis = true;
					break;
				default:
					FAIL_M(ssprintf("Invalid texture policy: %i", policy));
			}
		}

		/* This happens when we change themes; free all textures. */
		if (type == delayed_delete)
			bDeleteThis = true;

		if (bDeleteThis)
			DeleteTexture(t);
	}
	pathToTextureLock.Unlock();
}

void
RageTextureManager::ReloadAll()
{
	DisableOddDimensionWarning();

	/* Let's get rid of all unreferenced textures, so we don't reload a
	 * ton of cached data that we're not necessarily going to use. */
	DoDelayedDelete();

	pathToTextureLock.Lock();

	for (auto ID : m_mapPathToTexture) {
		ID.second->Reload();
	}

	pathToTextureLock.Unlock();

	EnableOddDimensionWarning();
}

/* In some cases, changing the display mode will reset the rendering context,
 * releasing all textures.  We don't want to reload immediately if that happens,
 * since we might be changing texture preferences too, which also may have to
 * reload textures.  Instead, tell all textures that their texture ID is
 * invalid, so it doesn't try to free it later when we really do reload (since
 * that ID might be associated with a different texture).  Ack. */
void
RageTextureManager::InvalidateTextures()
{
	pathToTextureLock.Lock();
	for (const auto& i : m_mapPathToTexture) {
		auto pTexture = i.second;
		pTexture->Invalidate();
	}
	pathToTextureLock.Unlock();
}

bool
RageTextureManager::SetPrefs(RageTextureManagerPrefs prefs)
{
	auto bNeedReload = false;
	if (m_Prefs != prefs)
		bNeedReload = true;

	m_Prefs = prefs;

	ASSERT(m_Prefs.m_iTextureColorDepth == 16 ||
		   m_Prefs.m_iTextureColorDepth == 32);
	ASSERT(m_Prefs.m_iMovieColorDepth == 16 ||
		   m_Prefs.m_iMovieColorDepth == 32);
	return bNeedReload;
}

void
RageTextureManager::DiagnosticOutput() const
{
	pathToTextureLock.LockShared();
	const unsigned iCount = m_mapPathToTexture.size();
	Locator::getLogger()->info("{} textures loaded:", iCount);

	auto iTotal = 0;
	for (const auto& i : m_mapPathToTexture) {
		const auto& ID = i.first;
		const RageTexture* pTex = i.second;

		auto sDiags = DISPLAY->GetTextureDiagnostics(pTex->GetTexHandle());
		auto sStr = ssprintf("%3ix%3i (%2i)",
							 pTex->GetTextureHeight(),
							 pTex->GetTextureWidth(),
							 pTex->m_iRefCount);

		if (!sDiags.empty())
			sStr += " " + sDiags;

		Locator::getLogger()->info(
		  " {:<40s} {}", sStr.c_str(), Basename(ID.filename).c_str());
		iTotal += pTex->GetTextureHeight() * pTex->GetTextureWidth();
	}
	Locator::getLogger()->info("total {:3i} texels", iTotal);
	pathToTextureLock.UnlockShared();
}

// lua start
#include "Etterna/Models/Lua/LuaBinding.h"

/** @brief Allow Lua to have access to the RageTextureManager. */
class LunaRageTextureManager : public Luna<RageTextureManager>
{
  public:
	static int AsyncLoadTexture(T* p, lua_State* L)
	{
		RageTextureID ID(SArg(1));
		// TODO: This should not be this

		// Copy-pasted from Sprite::SongBGTexture(ID)
		ID.bMipMaps = true;

		/* Song backgrounds are, by definition, in the background, so there's no
		 * need to keep alpha. */
		ID.iAlphaBits = 0;

		/* By default, song graphics are volatile: they're removed after one
		 * use. This is because some screens iteratively load and display lots
		 * of them (eg. ScreenSelectMusic,, ScreenEditMenu) one at a time, and
		 * we don't want to have hundreds of banners loaded at once. */
		ID.Policy = RageTextureID::TEX_VOLATILE;

		ID.bDither = true;

		TEXTUREMAN->AdjustTextureID(ID);
		p->AsyncLoadTexture(ID);
		return 0;
	}
	LunaRageTextureManager() { ADD_METHOD(AsyncLoadTexture); }
};

LUA_REGISTER_CLASS(RageTextureManager)
// lua end
