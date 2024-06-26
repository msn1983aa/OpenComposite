#pragma once
#include "BaseCommon.h"

enum OOVR_EVRSettingsError {
	VRSettingsError_None = 0,
	VRSettingsError_IPCFailed = 1,
	VRSettingsError_WriteFailed = 2,
	VRSettingsError_ReadFailed = 3,
	VRSettingsError_JsonParseFailed = 4,
	VRSettingsError_UnsetSettingHasNoDefault = 5, // This will be returned if the setting does not appear in the appropriate default file and has not been set
};

class BaseSettings {
private:
public:
	typedef OOVR_EVRSettingsError EVRSettingsError;

	const char* GetSettingsErrorNameFromEnum(EVRSettingsError eError);

	// Returns true if file sync occurred (force or settings dirty)
	bool Sync(bool bForce = false, EVRSettingsError* peError = nullptr);

	void SetBool(const char* pchSection, const char* pchSettingsKey, bool bValue, EVRSettingsError* peError = nullptr);
	void SetInt32(const char* pchSection, const char* pchSettingsKey, int32_t nValue, EVRSettingsError* peError = nullptr);
	void SetFloat(const char* pchSection, const char* pchSettingsKey, float flValue, EVRSettingsError* peError = nullptr);
	void SetString(const char* pchSection, const char* pchSettingsKey, const char* pchValue, EVRSettingsError* peError = nullptr);

	// Users of the system need to provide a proper default in default.vrsettings in the resources/settings/ directory
	// of either the runtime or the driver_xxx directory. Otherwise the default will be false, 0, 0.0 or ""
	bool GetBool(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	int32_t GetInt32(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	float GetFloat(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
	void GetString(const char* pchSection, const char* pchSettingsKey, VR_OUT_STRING() char* pchValue, uint32_t unValueLen, EVRSettingsError* peError = nullptr);

	void RemoveSection(const char* pchSection, EVRSettingsError* peError = nullptr);
	void RemoveKeyInSection(const char* pchSection, const char* pchSettingsKey, EVRSettingsError* peError = nullptr);
};
