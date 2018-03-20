// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved. 

#ifndef _TEXTURECOMPILER_H
#define _TEXTURECOMPILER_H

#include <stdio.h>     // strlen()
#include <functional>
#include <CryCore/StlUtils.h>
#include <string>
#include <deque>

#include <CryCore/ToolsHelpers/ResourceCompilerHelper.h>

//////////////////////////////////////////////////////////////////////////
// Provides settings and functions to make calls to RC to compile textures.
class CTextureCompiler : public CResourceCompilerHelper, NoCopy
{

private:
	static CryMutex          g_RCmutex;
	static CTextureCompiler* g_pRCInstance;

	static void __cdecl DestroyInstance();

	CTextureCompiler();
	~CTextureCompiler();

public:
	// factory-pattern
	static CTextureCompiler& GetInstance();

	// run compiler only on developer platform
#if defined(CRY_ENABLE_RC_HELPER)
public:
	// checks file date and existence
	// Return:
	//   fills processedFilename[] with name of the file that should be loaded
	//   boolean for success
	bool ProcessTextureIfNeeded(
	  const char* originalFilename,
	  char* processedFilename,
	  size_t processedFilenameSizeInBytes);

private:
	class CQueuedRC
	{
	public:
		CQueuedRC()
			: src()
			, dst()
			, windowed(false)
			, refresh(false)
			, returnval(eRcExitCode_Success)
			, process(INVALID_HANDLE_VALUE)
		{
		}

		string      src;
		string      dst;
		bool        windowed;
		bool        refresh;

		ERcExitCode returnval;
		HANDLE      process;
	};

	// notify-list, members will be notified about changes to the task-queue
	typedef std::set<IAsyncTextureCompileListener*> TProcListeners;

	// task-queue, can be used to query progress/todo, will deplete automagically
	typedef std::deque<CQueuedRC> TProcQueue;
	typedef CQueuedRC             TProcItem;

	// watch-list, can be used to reject duplicate requests, will deplete automagically
	#if 1
	typedef std::map<string, string>                                                               TWatchSet;
	#else
	typedef std::unordered_map<string, string, stl::hash_strcmp<string>, stl::hash_strcmp<string>> TWatchSet;
	#endif
	typedef TWatchSet::iterator                                                                    TWatchItem;

	// there are multiple instances of the helper, use global variables (1x per render-DLL)
	TWatchSet      m_mWatchList;
	TProcQueue     m_qProcessingList;
	TProcListeners m_sNotifyList;

	CryRWLock      m_rwLockWatch;
	CryRWLock      m_rwLockProcessing;
	CryRWLock      m_rwLockNotify;

	void*          m_jobqueue;

public:
	void ConsumeQueuedResourceCompiler(TProcItem* item);
	void ForkOffResourceCompiler(const char* szSrcFile, const char* szDstFile, const bool bWindow, const bool bRefresh);

	// Arguments:
	//   szSrcFile usually the path to a TIFF
	//   szDstFile usually the path to a DDS
	bool          HasQueuedResourceCompiler(const char* szSrcFile, const char* szDstFile);
	ERcCallResult QueueResourceCompiler(const char* szSrcFile, const char* szDstFile, const bool bWindow, const bool bRefresh);
#endif // CRY_ENABLE_RC_HELPER

	void AddAsyncTextureCompileListener(IAsyncTextureCompileListener* pListener);
	void RemoveAsyncTextureCompileListener(IAsyncTextureCompileListener* pListener);

private:
#if defined(CRY_ENABLE_RC_HELPER)
	// Arguments:
	//   szFilePath - could be source or destination filename
	//   dwIndex - used to iterate through all input filenames, start with 0 and increment by 1
	// Return:
	//   fills inputFilename[] with a filename (or empty string if that was the last input format)
	static void GetInputFilename(
	  const char* filename,
	  const unsigned int index,
	  char* inputFilename,
	  size_t inputFilenameSizeInBytes);
#endif

	// little helper function (to stay independent)
	static string AddSuffix(string in, const char* suffix)
	{
		string out = in;
		const char* extension = GetExtension(out.c_str());
		if (!extension)
			return out + suffix;

		size_t position = out.size() - strlen(extension) - 1;
		return out.insert(position, suffix);
	}

public:
	// only for image formats supported by the resource compiler
	// Arguments:
	//   szExtension - e.g. ".tif", can be 0
	static bool IsImageFormatSupported(const char* szExtension)
	{
		if (szExtension && (strlen(szExtension) > 0))
		{
			if (stricmp(szExtension, "dds") == 0 ||    // DirectX surface format
			    stricmp(szExtension, "hdr") == 0 ||    // High Dynamic Range image format
			    stricmp(szExtension, "tif") == 0)      // Crytek resource compiler image format
			{
				return (CRenderer::CV_r_texturecompiling > 0);
			}
		}

		return false;
	}
};

#endif
