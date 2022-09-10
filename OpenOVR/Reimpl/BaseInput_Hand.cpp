#include "stdafx.h"
#define BASE_IMPL
#include "BaseInput.h"

#include <convert.h>

#include <glm/ext.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/string_cast.hpp>

#include <optional>

// Think of this file as just a part of BaseInput.cpp.
// It's split out in pursuit of lower compile times.

using namespace vr;

// MODEL POSE STUFF
template <class T, class U>
static void quaternionCopy(const T& src, U& dst)
{
	dst.x = src.x;
	dst.y = src.y;
	dst.z = src.z;
	dst.w = src.w;
}

// Games that use this:
// * NeosVR
// Any others? Please add them to the list!
void BaseInput::ConvertHandModelSpace(const std::vector<XrHandJointLocationEXT>& joints, bool isRight, VRBoneTransform_t* output)
{
	// The root bone should just be left at identity? TODO check SteamVR
	output[eBone_Root].orientation = vr::HmdQuaternionf_t{ /* w */ 1, 0, 0, 0 };
	output[eBone_Root].position = vr::HmdVector4_t{ 0, 0, 0, 1 };

	// Note: go to https://gltf-viewer.donmccurdy.com/ and load up the hand glTF model from the test suite
	// Turn the axes on and compare it to the OpenXR hand tracking extension diagram:
	// https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#_conventions_of_hand_joints

	// The first transform we need is to place all the bones into the correct positions. We can swap a
	// few axes around to convert between the different coordinate systems used by SteamVR and OpenXR.
	glm::mat4 globalTransform = glm::zero<glm::mat4>();
	globalTransform[1][0] = -1; // +X in SteamVR comes from -Y in OpenXR
	globalTransform[0][1] = -1; // +Y in SteamVR comes from -X in OpenXR
	globalTransform[2][2] = -1; // +Z in SteamVR comes from -Z in OpenXR
	globalTransform[3][3] = 1;

	// We also need to apply a local transform to the coordinate spaces of the non-thumb fingers. This is because
	// the bones are set up in such a way that their local-to-the-bone coordinate system that the geometry works
	// in varies between SteamVR and OpenXR. Oddly this is rotated 90deg around the Y axis (local to that bone),
	// which suggests it's not caused by rotating the bone.
	// This has to be the first transform applied when calculating the bone's rotation, since it needs to rotate
	// in the bone's local space rather than in model space.
	glm::mat4 localTransform = glm::rotate(glm::identity<glm::mat4>(), glm::radians(-90.f), { 0, 1, 0 });

	// The wrist bone has it's own special transform, with all axes negated
	glm::mat4 rightWristTransform = glm::scale(glm::identity<glm::mat4>(), { -1, -1, -1 });

	// Wrists are a special case of being different between sides
	glm::mat4 leftWristTransform = glm::zero<glm::mat4>();
	leftWristTransform[1][0] = 1;
	leftWristTransform[0][1] = 1;
	leftWristTransform[2][2] = -1;
	leftWristTransform[3][3] = 1;

	// And the left hand gets it's own special transform
	glm::mat4 leftHandTransform = glm::scale(glm::identity<glm::mat4>(), { -1, -1, 1 });

	for (int XrId = XR_HAND_JOINT_WRIST_EXT; XrId <= XR_HAND_JOINT_LITTLE_TIP_EXT; XrId++) {
		const XrHandJointLocationEXT& this_joint = joints[XrId];
		glm::mat4 pose = globalTransform * X2G_om34_pose(this_joint.pose);

		// Not a bug - xrIds match vrIds except for palm pose and aux bones.
		vr::VRBoneTransform_t& out = output[XrId];

		// Position is easy enough...
		out.position = { pose[3][0], pose[3][1], pose[3][2], 1.f };

		// But the transform to correct the bone's local coordinate system varies between bones, so add
		// that in here. It's also different on the left and right hands.
		if (XrId == XR_HAND_JOINT_WRIST_EXT) {
			if (isRight) {
				pose *= rightWristTransform;
			} else {
				pose *= leftWristTransform;
			}
		} else {
			pose *= localTransform;

			if (!isRight) {
				pose *= leftHandTransform;
			}
		}

		glm::quat out_rotation(pose);
		quaternionCopy(out_rotation, out.orientation);
	}

	OOVR_SOFT_ABORT("Aux bones not yet implemented!");
}

// END MODEL POSE STUFF

// RELATIVE POSE STUFF

// This is just exactly what OC did before. It probably doesn't do the right thing.
void BaseInput::ConvertHandParentSpace(const std::vector<XrHandJointLocationEXT>& joints, bool isRight, VRBoneTransform_t* out_transforms)
{
	// Annoyingly the coordinate system between this extension and OpenVR is also different. As per the wiki page:
	// https://github.com/ValveSoftware/openvr/wiki/Hand-Skeleton
	// The hands effectively sit on their sides in the bind pose. The right hand's palm faces -X and the left hand's
	// palm faces +X. In the OpenXR extension, the hands are as they would be if you placed them on a table: palms on
	// both hands are down (-Y) and the tops of your hands are up (+Y). Therefore the normal of your thumbnails are
	// facing towards the opposite hand. Fortunately Z represents the same axis in both coordinate spaces.
	// Therefore roll the right-hand clockwise 90deg around Z and the left hand counter-clockwise around Z (careful with
	// what direction Z is if you're trying to visualise this).
	float angle_mult;
	if (!isRight) {
		angle_mult = 1.0f; // Natural rotation is CCW, invert for clockwise
	} else {
		angle_mult = -1.0f;
	}
	glm::mat4 systemTransform = glm::rotate(glm::identity<glm::mat4>(), angle_mult * math_pi / 2.0f, glm::vec3(0, 0, 1));

	// Load the data into the output bones, with the correct mapping
	std::optional<XrHandJointEXT> parentId;
	auto mapBone = [&](XrHandJointEXT xrId, int vrId) {
		const XrHandJointLocationEXT& src = joints.at(xrId);
		OOVR_FALSE_ABORT(vrId < 31);
		vr::VRBoneTransform_t& out = out_transforms[vrId];

		// Read the OpenXR transform
		// I don't think there's anything we can do if the validity flags are false, so just ignore them
		glm::mat4 pose = systemTransform * X2G_om34_pose(src.pose);

		// All the OpenXR transforms are relative to the space we specified as baseSpace, in this case the grip pose. If the
		// application wants each bone's transform relative to it's parent, apply that now.
		// If this bone is the root bone (parentId is not set), then it's the same in either space mode.
		// Get the required transform from:
		// Tbone_in_model = Tparent_in_model * Tbone_in_parent
		// inv(Tparent_in_model) * Tbone_in_model = Tbone_in_parent
		if (parentId) {
			const XrHandJointLocationEXT& parent = joints.at(parentId.value());
			glm::mat4 parentPose = systemTransform * X2G_om34_pose(parent.pose);

			pose = glm::affineInverse(parentPose) * pose;
		}

		// TODO eMotionRange, if that's even possible

		glm::quat rotation(pose);
		out.position = vr::HmdVector4_t{ pose[3][0], pose[3][1], pose[3][2], 1.f }; // What's the fourth value for?
		out.orientation = vr::HmdQuaternionf_t{ rotation.w, rotation.x, rotation.y, rotation.z };

		// Update the parent to make it convenient to declare bones moving towards the finger tip
		parentId = xrId;
	};

	// Set up the root bone
	out_transforms[0].orientation = vr::HmdQuaternionf_t{ /* w */ 1, 0, 0, 0 };
	out_transforms[0].position = vr::HmdVector4_t{ 0, 0, 0, 1 };

	parentId = {};
	mapBone(XR_HAND_JOINT_WRIST_EXT, eBone_Wrist);

	// clang-format off
	parentId = XR_HAND_JOINT_WRIST_EXT;
	mapBone(XR_HAND_JOINT_THUMB_METACARPAL_EXT, eBone_Thumb0);
	mapBone(XR_HAND_JOINT_THUMB_PROXIMAL_EXT,   eBone_Thumb1);
	mapBone(XR_HAND_JOINT_THUMB_DISTAL_EXT,     eBone_Thumb2);
	mapBone(XR_HAND_JOINT_THUMB_TIP_EXT,        eBone_Thumb3);

	parentId = XR_HAND_JOINT_WRIST_EXT;
	mapBone(XR_HAND_JOINT_INDEX_METACARPAL_EXT,   eBone_IndexFinger0);
	mapBone(XR_HAND_JOINT_INDEX_PROXIMAL_EXT,     eBone_IndexFinger1);
	mapBone(XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, eBone_IndexFinger2);
	mapBone(XR_HAND_JOINT_INDEX_DISTAL_EXT,       eBone_IndexFinger3);
	mapBone(XR_HAND_JOINT_INDEX_TIP_EXT,          eBone_IndexFinger4);

	parentId = XR_HAND_JOINT_WRIST_EXT;
	mapBone(XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,   eBone_MiddleFinger0);
	mapBone(XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,     eBone_MiddleFinger1);
	mapBone(XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, eBone_MiddleFinger2);
	mapBone(XR_HAND_JOINT_MIDDLE_DISTAL_EXT,       eBone_MiddleFinger3);
	mapBone(XR_HAND_JOINT_MIDDLE_TIP_EXT,          eBone_MiddleFinger4);

	parentId = XR_HAND_JOINT_WRIST_EXT;
	mapBone(XR_HAND_JOINT_RING_METACARPAL_EXT,   eBone_RingFinger0);
	mapBone(XR_HAND_JOINT_RING_PROXIMAL_EXT,     eBone_RingFinger1);
	mapBone(XR_HAND_JOINT_RING_INTERMEDIATE_EXT, eBone_RingFinger2);
	mapBone(XR_HAND_JOINT_RING_DISTAL_EXT,       eBone_RingFinger3);
	mapBone(XR_HAND_JOINT_RING_TIP_EXT,          eBone_RingFinger4);

	parentId = XR_HAND_JOINT_WRIST_EXT;
	mapBone(XR_HAND_JOINT_LITTLE_METACARPAL_EXT,   eBone_PinkyFinger0);
	mapBone(XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,     eBone_PinkyFinger1);
	mapBone(XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, eBone_PinkyFinger2);
	mapBone(XR_HAND_JOINT_LITTLE_DISTAL_EXT,       eBone_PinkyFinger3);
	mapBone(XR_HAND_JOINT_LITTLE_TIP_EXT,          BaseInput::eBone_PinkyFinger4);
	// clang-format on

	// TODO aux bones - they're equal to the distal bones but always use VRSkeletalTransformSpace_Model mode
}
