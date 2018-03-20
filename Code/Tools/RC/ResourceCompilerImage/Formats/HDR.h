// Copyright 2001-2017 Crytek GmbH / Crytek Group. All rights reserved.
#pragma once

///////////////////////////////////////////////////////////////////////////////////
// Image i/o using Greg Ward's RGBE reader and writer

#include <vector>

#include <assert.h>
#include <CryString/CryString.h>

class CImageProperties;
class ImageObject;

namespace ImageHDR
{
	ImageObject* LoadByUsingHDRLoader(const char* filenameRead, CImageProperties* pProps, string& res_specialInstructions);

	bool SaveByUsingHDRSaver(const char* filenameWrite, const CImageProperties* pProps, const ImageObject* pImageObject);
};
