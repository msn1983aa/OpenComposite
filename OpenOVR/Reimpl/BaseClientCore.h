#pragma once
#include "BaseCommon.h"

#include <string>

class BaseClientCore {
public:
	static bool CheckAppEnabled();
	static std::string GetAlternativeRuntimePath();
	void SetManifestPath(std::string filename);
	static inline vr::EVRApplicationType appType = vr::VRApplication_Max;

private:
	static std::string GetAppPath();
	static std::wstring GetDllDir();

public:
	/** Initializes the system */
	vr::EVRInitError Init(vr::EVRApplicationType eApplicationType, const char* pStartupInfo);

	/** cleans up everything in vrclient.dll and prepares the DLL to be unloaded */
	void Cleanup();

	/** checks to see if the specified interface/version is supported in this vrclient.dll */
	vr::EVRInitError IsInterfaceVersionValid(const char* pchInterfaceVersion);

	/** Retrieves any interface from vrclient.dll */
	void* GetGenericInterface(const char* pchNameAndVersion, vr::EVRInitError* peError);

	/** Returns true if any driver has an HMD attached. Can be called outside of Init/Cleanup */
	bool BIsHmdPresent();

	/** Returns an English error string from inside vrclient.dll which might be newer than the API DLL */
	const char* GetEnglishStringForHmdError(vr::EVRInitError eError);

	/** Returns an error symbol from inside vrclient.dll which might be newer than the API DLL */
	const char* GetIDForVRInitError(vr::EVRInitError eError);
};
