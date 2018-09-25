#include "stdafx.h"
#define BASE_IMPL
#include "BaseRenderModels.h"
#include "resources.h"
#include "convert.h"
#include "Misc/Config.h"

// Used for the hand offsets
#include "BaseCompositor.h"

#include <string>
#include <sstream>
#include <vector>

using namespace std;
using namespace OVR;

#pragma region structs

enum OOVR_EVRRenderModelError {
	VRRenderModelError_None = 0,
	VRRenderModelError_Loading = 100,
	VRRenderModelError_NotSupported = 200,
	VRRenderModelError_InvalidArg = 300,
	VRRenderModelError_InvalidModel = 301,
	VRRenderModelError_NoShapes = 302,
	VRRenderModelError_MultipleShapes = 303,
	VRRenderModelError_TooManyVertices = 304,
	VRRenderModelError_MultipleTextures = 305,
	VRRenderModelError_BufferTooSmall = 306,
	VRRenderModelError_NotEnoughNormals = 307,
	VRRenderModelError_NotEnoughTexCoords = 308,

	VRRenderModelError_InvalidTexture = 400,
};

struct OOVR_RenderModel_Vertex_t {
	vr::HmdVector3_t vPosition;		// position in meters in device space
	vr::HmdVector3_t vNormal;
	float rfTextureCoord[2];
};

#if defined(__linux__) || defined(__APPLE__) 
// This structure was originally defined mis-packed on Linux, preserved for 
// compatibility. 
#pragma pack( push, 4 )
#endif

struct OOVR_RenderModel_t {
	const OOVR_RenderModel_Vertex_t *rVertexData;	// Vertex data for the mesh
	uint32_t unVertexCount;						// Number of vertices in the vertex data
	const uint16_t *rIndexData;					// Indices into the vertex data for each triangle
	uint32_t unTriangleCount;					// Number of triangles in the mesh. Index count is 3 * TriangleCount
	OOVR_TextureID_t diffuseTextureId;			// Session unique texture identifier. Rendermodels which share the same texture will have the same id. <0 == texture not present
};

struct OOVR_RenderModel_TextureMap_t {
	uint16_t unWidth, unHeight; // width and height of the texture map in pixels
	const uint8_t *rubTextureMapData;	// Map texture data. All textures are RGBA with 8 bits per channel per pixel. Data size is width * height * 4ub
};

#if defined(__linux__) || defined(__APPLE__) 
#pragma pack( pop )
#endif

typedef uint32_t VRComponentProperties;
enum OOVR_EVRComponentProperty {
	VRComponentProperty_IsStatic = (1 << 0),
	VRComponentProperty_IsVisible = (1 << 1),
	VRComponentProperty_IsTouched = (1 << 2),
	VRComponentProperty_IsPressed = (1 << 3),
	VRComponentProperty_IsScrolled = (1 << 4),
};

/** Describes state information about a render-model component, including transforms and other dynamic properties */
struct OOVR_RenderModel_ComponentState_t {
	vr::HmdMatrix34_t mTrackingToComponentRenderModel;  // Transform required when drawing the component render model
	vr::HmdMatrix34_t mTrackingToComponentLocal;        // Transform available for attaching to a local component coordinate system (-Z out from surface )
	VRComponentProperties uProperties; // See OOVR_EVRComponentProperty
};

#pragma endregion

typedef OOVR_RenderModel_t RenderModel_t;
typedef OOVR_EVRRenderModelError EVRRenderModelError;
typedef OOVR_RenderModel_TextureMap_t RenderModel_TextureMap_t;
typedef OOVR_TextureID_t TextureID_t;

static string loadResource(int rid) {
	// Open our OBJ file
	HRSRC ref = FindResource(openovr_module_id, MAKEINTRESOURCE(rid), MAKEINTRESOURCE(RES_T_OBJ));
	if (!ref) {
		string err = "FindResource error: " + std::to_string(GetLastError());
		OOVR_ABORT(err.c_str());
	}

	char *cstr = (char*)LoadResource(openovr_module_id, ref);
	if (!cstr) {
		string err = "LoadResource error: " + std::to_string(GetLastError());
		OOVR_ABORT(err.c_str());
	}

	DWORD len = SizeofResource(openovr_module_id, ref);
	if (!len) {
		string err = "SizeofResource error: " + std::to_string(GetLastError());
		OOVR_ABORT(err.c_str());
	}

	return string(cstr, len);
}

static OOVR_RenderModel_Vertex_t split_face(
		const string &s,
		const vector<vr::HmdVector3_t> &verts,
		const vector<vr::HmdVector2_t> &uvs,
		const vector<vr::HmdVector3_t> &normals) {

	size_t slash1 = s.find('/');
	size_t slash2 = s.find('/', slash1 + 1);

	if (slash1 == string::npos || slash2 == string::npos) {
		string err = "Bad face spec: " + s;
		OOVR_ABORT(err.c_str());
	}

	int vert = stoi(s.substr(0, slash1));
	int uv = stoi(s.substr(slash1 + 1, slash1 - slash2 - 1));
	int norm = stoi(s.substr(slash2 + 1));

	// OBJ references start at one
	vert--;
	uv--;
	norm--;

	// Build the result
	OOVR_RenderModel_Vertex_t out = { 0 };
	out.vPosition = verts[vert];
	out.vNormal = normals[norm];
	out.rfTextureCoord[0] = uvs[uv].v[0];
	out.rfTextureCoord[1] = uvs[uv].v[1];
	return out;
}

EVRRenderModelError BaseRenderModels::LoadRenderModel_Async(const char * pchRenderModelName, RenderModel_t ** renderModel) {
	string name = pchRenderModelName;
	int rid;
	int sided;

	if (name == "renderLeftHand") {
		rid = RES_O_HAND_LEFT;
		sided = 1;
	}
	else if (name == "renderRightHand") {
		rid = RES_O_HAND_RIGHT;
		sided = -1;
	}
	else {
		string err = "Unknown render model name: " + string(pchRenderModelName);
		OOVR_ABORT(err.c_str());
		return VRRenderModelError_None;
	}

	istringstream res = istringstream(loadResource(rid));

	vector<vr::HmdVector3_t> verts;
	vector<vr::HmdVector2_t> uvs;
	vector<vr::HmdVector3_t> normals;
	vector<OOVR_RenderModel_Vertex_t> vertexData;

	// Transform to line up the model with the Touch controller
	Matrix4f modelTransform = Matrix4f(Quatf(Vector3f(0, 0, 1), sided * math_pi / 2));
	modelTransform.SetTranslation(Vector3f(sided * 0.015f, 0.0f, 0.03f));

	// SteamVR rotates it's models 180deg around the Y axis for some reason
	modelTransform *= Matrix4f(Quatf(Vector3f(0, 1, 0), math_pi));

	Matrix4f transform = BaseCompositor::GetHandTransform().Inverted() * modelTransform;
	Quatf rotate = Quatf(transform);

	while (!res.eof()) {
		string op;
		res >> op;

		if (op == "v") {
			// Vertex
			Vector3f v;
			res >> v.x >> v.y >> v.z;

			// Maya exports in cm, so translate that to meters
			v *= 0.01f;

			// Transform from the OVR pose to the SteamVR pose, and rotate the hand model at the same time
			v = transform.Transform(v);

			vr::HmdVector3_t res;
			O2S_v3f(v, res);
			verts.push_back(res);
		}
		else if (op == "vt") {
			// UV
			float x, y;
			res >> x >> y;
			uvs.push_back(vr::HmdVector2_t{ x, y });
		}
		else if (op == "vn") {
			// Normal
			Vector3f v;
			res >> v.x >> v.y >> v.z;

			// Transform from the OVR pose to the SteamVR pose
			// Don't translate it though, since it's a normal
			v = rotate * v;

			vr::HmdVector3_t res;
			O2S_v3f(v, res);
			normals.push_back(res);
		}
		else if (op == "f") {
			// Face
			string a, b, c;
			res >> a >> b >> c;

			vertexData.push_back(split_face(a, verts, uvs, normals));
			vertexData.push_back(split_face(b, verts, uvs, normals));
			vertexData.push_back(split_face(c, verts, uvs, normals));
		}
	}

	*renderModel = new RenderModel_t();
	RenderModel_t &rm = **renderModel;

	rm.unVertexCount = (uint32_t) vertexData.size();
	OOVR_RenderModel_Vertex_t *vertexData_arr = new OOVR_RenderModel_Vertex_t[rm.unVertexCount];
	rm.rVertexData = vertexData_arr;
	for (uint32_t i = 0; i < rm.unVertexCount; i++) {
		vertexData_arr[i] = vertexData[i];
	}

	uint16_t *indexData = new uint16_t[rm.unVertexCount];
	for (uint16_t i = 0; i < rm.unVertexCount; i++) {
		indexData[i] = i;
	}
	rm.rIndexData = indexData;
	rm.unTriangleCount = rm.unVertexCount / 3;

	// Texture
	rm.diffuseTextureId = -1; // Disabled for now

	return VRRenderModelError_None;
}

void BaseRenderModels::FreeRenderModel(RenderModel_t * renderModel) {
	delete renderModel->rVertexData;
	delete renderModel->rIndexData;
	delete renderModel;
}

EVRRenderModelError BaseRenderModels::LoadTexture_Async(TextureID_t textureId, RenderModel_TextureMap_t ** texture) {
	*texture = new RenderModel_TextureMap_t();
	RenderModel_TextureMap_t &tx = **texture;

	// For now use a 1x1 single coloured texture
	tx.unWidth = 1;
	tx.unHeight = 1;
	uint8_t *d = new uint8_t[tx.unWidth * tx.unHeight * 4];
	tx.rubTextureMapData = d;

	vr::HmdColor_t colour = oovr_global_configuration.HandColour();
	d[0] = (uint8_t)(colour.r * 255);
	d[1] = (uint8_t)(colour.g * 255);
	d[2] = (uint8_t)(colour.b * 255);
	d[3] = (uint8_t)(colour.a * 255);

	return VRRenderModelError_None;
}

void BaseRenderModels::FreeTexture(RenderModel_TextureMap_t * texture) {
	delete texture->rubTextureMapData;
	delete texture;
}

EVRRenderModelError BaseRenderModels::LoadTextureD3D11_Async(TextureID_t textureId, void * pD3D11Device, void ** ppD3D11Texture2D) {
	STUBBED();
}

EVRRenderModelError BaseRenderModels::LoadIntoTextureD3D11_Async(TextureID_t textureId, void * pDstTexture) {
	STUBBED();
}

void BaseRenderModels::FreeTextureD3D11(void * pD3D11Texture2D) {
	STUBBED();
}

uint32_t BaseRenderModels::GetRenderModelName(uint32_t unRenderModelIndex, VR_OUT_STRING() char * pchRenderModelName, uint32_t unRenderModelNameLen) {
	STUBBED();
}

uint32_t BaseRenderModels::GetRenderModelCount() {
	STUBBED();
}

uint32_t BaseRenderModels::GetComponentCount(const char * pchRenderModelName) {
	// Left at zero for now until I can properly test it, and add textures
	return oovr_global_configuration.RenderCustomHands() ? 1 : 0;

	// This means there are no moving components (eg buttons thumbstick etc) which
	//  can be animated via the Component functions, which thus shouldn't be called.
}

uint32_t BaseRenderModels::GetComponentName(const char * pchRenderModelName, uint32_t unComponentIndex,
	char * pchComponentName, uint32_t unComponentNameLen) {

	string name = pchRenderModelName;

	if (name != "renderLeftHand" && name != "renderRightHand") {
		string err = "Unknown render model name: " + string(pchRenderModelName);
		OOVR_ABORT(err.c_str());
		return VRRenderModelError_None;
	}

	// Only the first component exists
	if (unComponentIndex != 0) {
		return 0;
	}

	if (pchComponentName) {
		// +1 for NULL
		if (unComponentNameLen < name.length() + 1) {
			OOVR_ABORT("unComponentNameLen too small!");
		}

		// TODO should we allow too small buffers?
		strcpy_s(pchComponentName, unComponentNameLen, name.c_str());
		pchComponentName[unComponentNameLen - 1] = 0;
	}

	// +1 for null
	return (uint32_t) name.length() + 1;
}

uint64_t BaseRenderModels::GetComponentButtonMask(const char * pchRenderModelName, const char * pchComponentName) {
	STUBBED();
}

uint32_t BaseRenderModels::GetComponentRenderModelName(const char * pchRenderModelName, const char * pchComponentName,
	char * componentModelName, uint32_t componentModelNameLen) {

	string name = pchRenderModelName;
	if (name != "renderLeftHand" && name != "renderRightHand") {
		string err = "Unknown render model name: " + string(pchRenderModelName);
		OOVR_ABORT(err.c_str());
		return VRRenderModelError_None;
	}

	if (name != pchComponentName) {
		OOVR_ABORT("pchRenderModelName and pchComponentName mismatch");
	}

	if (componentModelName) {
		// +1 for NULL
		if (componentModelNameLen < name.length() + 1) {
			OOVR_ABORT("componentModelNameLen too small!");
		}

		// TODO should we allow too small buffers?
		strcpy_s(componentModelName, componentModelNameLen, name.c_str());
		componentModelName[componentModelNameLen - 1] = 0;
	}

	// +1 for null
	return (uint32_t) name.length() + 1;
}

bool BaseRenderModels::GetComponentState(const char * pchRenderModelName, const char * pchComponentName,
	const vr::VRControllerState_t * pControllerState,const OOVR_RenderModel_ControllerMode_State_t * pState,
	OOVR_RenderModel_ComponentState_t * pComponentState) {

	vr::HmdMatrix34_t ident = { 0 };
	ident.m[0][0] = ident.m[1][1] = ident.m[2][2] = 1;

	pComponentState->mTrackingToComponentLocal = ident;
	pComponentState->mTrackingToComponentRenderModel = ident;

	pComponentState->uProperties = VRComponentProperty_IsVisible | VRComponentProperty_IsStatic;

	return true;
}

bool BaseRenderModels::RenderModelHasComponent(const char * pchRenderModelName, const char * pchComponentName) {
	STUBBED();
}

uint32_t BaseRenderModels::GetRenderModelThumbnailURL(const char * pchRenderModelName, VR_OUT_STRING() char * pchThumbnailURL, uint32_t unThumbnailURLLen, EVRRenderModelError * peError) {
	if (peError)
		*peError = VRRenderModelError_None;

	STUBBED();
}

uint32_t BaseRenderModels::GetRenderModelOriginalPath(const char * pchRenderModelName, VR_OUT_STRING() char * pchOriginalPath, uint32_t unOriginalPathLen, EVRRenderModelError * peError) {
	if (peError)
		*peError = VRRenderModelError_None;

	STUBBED();
}

const char * BaseRenderModels::GetRenderModelErrorNameFromEnum(EVRRenderModelError error) {
	STUBBED();
}
