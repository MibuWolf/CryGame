﻿using CryEngine.Common;

namespace CryEngine.FileSystem
{
	/// <summary>
	/// File handle for the native ICryPak system.
	/// </summary>
	public class CryPakFile
	{
		private readonly _CrySystem_cs_SWIGTYPE_p_FILE _nativeHandle;

		internal CryPakFile(_CrySystem_cs_SWIGTYPE_p_FILE handle)
		{
			_nativeHandle = handle;
		}

		public static implicit operator _CrySystem_cs_SWIGTYPE_p_FILE(CryPakFile file)
		{
			return file?._nativeHandle;
		}	}
}
