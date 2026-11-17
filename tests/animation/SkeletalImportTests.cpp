// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// Headless checks for A1: import a rigged glTF into a Skeleton and an Animation, round-trip both
// through their .lskel / .lanim blobs, and refuse malformed blobs. No GPU, editor or project.

#include "lpch.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Lux/Animation/SkinPalette.h"
#include "Lux/Asset/SkeletalImporter.h"
#include "Lux/Serialization/FileStream.h"

#include <glm/gtc/matrix_transform.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("skeletal-test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
}
using namespace Lux;

namespace
{
	int g_Failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; g_Failures++; } } while (0)

	bool Near(float a, float b, float eps = 1e-5f) { return std::abs(a - b) <= eps; }

	template<typename T>
	std::filesystem::path Save(const Ref<T>& asset, const std::filesystem::path& path)
	{
		{
			FileStreamWriter writer(path);
			asset->Serialize(&writer);
			CHECK(writer.IsStreamGood());
		}
		return path;
	}

	template<typename T>
	bool Load(Ref<T>& out, const std::filesystem::path& path)
	{
		FileStreamReader reader(path);
		out = Ref<T>::Create();
		return reader.IsStreamGood() && out->Deserialize(&reader);
	}

	void SameSkeleton(const Skeleton& a, const Skeleton& b)
	{
		CHECK(a.GetNumBones() == b.GetNumBones());
		for (uint32_t i = 0; i < a.GetNumBones() && i < b.GetNumBones(); i++)
		{
			CHECK(a.GetBoneName(i) == b.GetBoneName(i));
			CHECK(a.GetParentBoneIndex(i) == b.GetParentBoneIndex(i));
			CHECK(a.GetBoneTranslations()[i] == b.GetBoneTranslations()[i]);
			CHECK(a.GetBoneRotations()[i] == b.GetBoneRotations()[i]);
			CHECK(a.GetBoneScales()[i] == b.GetBoneScales()[i]);
			CHECK(a.GetModelSpaceRestPose()[i] == b.GetModelSpaceRestPose()[i]);
		}
	}

	void SameClip(const Animation& a, const Animation& b)
	{
		CHECK(a.GetName() == b.GetName());
		CHECK(a.GetDuration() == b.GetDuration());
		CHECK(a.GetSkeletonHandle() == b.GetSkeletonHandle());
		CHECK(a.GetNumKeys() == b.GetNumKeys());
		CHECK(a.GetTracks().size() == b.GetTracks().size());
		for (size_t t = 0; t < a.GetTracks().size() && t < b.GetTracks().size(); t++)
		{
			const BoneTrack& x = a.GetTracks()[t];
			const BoneTrack& y = b.GetTracks()[t];
			CHECK(x.BoneIndex == y.BoneIndex);
			CHECK(x.Translations.size() == y.Translations.size() && x.Rotations.size() == y.Rotations.size() && x.Scales.size() == y.Scales.size());
			for (size_t k = 0; k < x.Rotations.size() && k < y.Rotations.size(); k++)
				CHECK(x.Rotations[k].Time == y.Rotations[k].Time && x.Rotations[k].Value == y.Rotations[k].Value);
			for (size_t k = 0; k < x.Translations.size() && k < y.Translations.size(); k++)
				CHECK(x.Translations[k].Time == y.Translations[k].Time && x.Translations[k].Value == y.Translations[k].Value);
			for (size_t k = 0; k < x.Scales.size() && k < y.Scales.size(); k++)
				CHECK(x.Scales[k].Time == y.Scales[k].Time && x.Scales[k].Value == y.Scales[k].Value);
		}
	}
}

// The rest-pose skinned bounds of a model: every vertex through its bone palette, as the renderer's skinning
// pass computes it. Also the number of mesh bones, to compare formats.
static bool SkinnedRestBounds(const std::filesystem::path& path, glm::vec3& low, glm::vec3& high, size_t& meshBones, float& paletteSpread)
{
	Ref<Skeleton> skeleton;
	std::vector<Ref<Animation>> clips;
	if (!ImportSkeletonAndAnimations(path, skeleton, clips) || !skeleton)
		return false;
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path.string(), k_MeshImportFlags);
	if (!scene)
		return false;
	std::vector<uint32_t> bases;
	uint32_t total = 0;
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		bases.push_back(total);
		total += scene->mMeshes[m]->mNumVertices;
	}
	std::vector<BoneInfluence> influences;
	std::vector<BoneInfo> bones;
	if (!ExtractSkinWeights(*scene, bases, total, influences, bones) || influences.empty())
		return false;
	const std::vector<glm::mat4> palette = BuildSkinPalette(*skeleton, BuildModelPose(*skeleton), bones);
	paletteSpread = 0.0f;
	for (const glm::mat4& matrix : palette)
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++)
				paletteSpread = std::max(paletteSpread, std::abs(matrix[c][r] - palette.front()[c][r]));
	meshBones = bones.size();
	low = glm::vec3(1e30f);
	high = glm::vec3(-1e30f);
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		for (uint32_t v = 0; v < scene->mMeshes[m]->mNumVertices; v++)
		{
			const aiVector3D& p = scene->mMeshes[m]->mVertices[v];
			const BoneInfluence& influence = influences[bases[m] + v];
			glm::mat4 skin(0.0f);
			for (int slot = 0; slot < 4; slot++)
				skin += palette[influence.BoneInfoIndices[slot]] * influence.Weights[slot];
			const glm::vec3 world = glm::vec3(skin * glm::vec4(p.x, p.y, p.z, 1.0f));
			low = glm::min(low, world);
			high = glm::max(high, world);
		}
	}
	return true;
}

// LuxBiped is one model in two files. A binary FBX (units, node scale, axes of its own) and a glTF must
// import to the same rig: the same mesh bones and clips, and the same skinned shape at rest.
static void CheckFormatsAgree(const std::filesystem::path& folder)
{
	glm::vec3 fbxLow, fbxHigh, gltfLow, gltfHigh;
	size_t fbxBones = 0, gltfBones = 0;
	float fbxSpread = 1.0f, gltfSpread = 1.0f;
	CHECK(SkinnedRestBounds(folder / "LuxBiped.fbx", fbxLow, fbxHigh, fbxBones, fbxSpread));
	CHECK(SkinnedRestBounds(folder / "LuxBiped.gltf", gltfLow, gltfHigh, gltfBones, gltfSpread));
	CHECK(fbxBones == gltfBones && fbxBones == 17);
	CHECK(fbxSpread < 1e-2f && gltfSpread < 1e-3f); // every bone agrees at rest (FBX in 100x matrices)
	for (int axis = 0; axis < 3; axis++)
	{
		CHECK(Near(fbxLow[axis], gltfLow[axis], 0.01f));
		CHECK(Near(fbxHigh[axis], gltfHigh[axis], 0.01f));
	}
	CHECK(Near(fbxHigh.y - fbxLow.y, 1.75f, 0.02f));

	Ref<Skeleton> fbxSkeleton, gltfSkeleton;
	std::vector<Ref<Animation>> fbxClips, gltfClips;
	CHECK(ImportSkeletonAndAnimations(folder / "LuxBiped.fbx", fbxSkeleton, fbxClips));
	CHECK(ImportSkeletonAndAnimations(folder / "LuxBiped.gltf", gltfSkeleton, gltfClips));
	CHECK(fbxClips.size() == 2 && gltfClips.size() == 2);
	if (fbxSkeleton && gltfSkeleton && fbxClips.size() == 2 && gltfClips.size() == 2)
	{
		for (uint32_t i = 0; i < gltfSkeleton->GetNumBones(); i++)
			CHECK(fbxSkeleton->GetBoneIndex(gltfSkeleton->GetBoneName(i)) != Skeleton::NullIndex);
		for (size_t i = 0; i < 2; i++)
			CHECK(Near(fbxClips[i]->GetDuration(), gltfClips[i]->GetDuration(), 0.05f)); // the exporters pad the end differently
	}

	// A model that is only on this machine (licensed, not in the repo) gets the same sanity checks.
	const std::filesystem::path local = folder / "local" / "X Bot.fbx";
	if (std::filesystem::exists(local))
	{
		glm::vec3 low, high;
		size_t bones = 0;
		float spread = 1.0f;
		CHECK(SkinnedRestBounds(local, low, high, bones, spread));
		CHECK(high.y - low.y > 1.2f && high.y - low.y < 2.4f);
		std::cout << "local FBX '" << local.filename().string() << "': " << bones << " mesh bones, height " << (high.y - low.y) << " m" << std::endl;
	}
}

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::cerr << "usage: SkeletalImportTests <RiggedSimple.gltf> <output dir>\n";
		return 2;
	}
	const std::filesystem::path model = argv[1];
	const std::filesystem::path out = argv[2];

	// --- import ---------------------------------------------------------------------------------
	Ref<Skeleton> skeleton;
	std::vector<Ref<Animation>> clips;
	CHECK(ImportSkeletonAndAnimations(model, skeleton, clips));
	CHECK(skeleton);
	if (!skeleton || clips.size() != 1)
	{
		std::cerr << "FAIL: expected a skeleton and exactly 1 clip, got " << clips.size() << "\n";
		return 1;
	}

	// RiggedSimple: joints "Bone" and its child "Bone.001", under the Armature and Z_UP nodes that the
	// skeleton keeps so its model space matches the inverse bind poses.
	const uint32_t bone = skeleton->GetBoneIndex("Bone");
	const uint32_t bone1 = skeleton->GetBoneIndex("Bone.001");
	CHECK(skeleton->GetNumBones() >= 4);
	CHECK(bone != Skeleton::NullIndex && bone1 != Skeleton::NullIndex);
	CHECK(skeleton->GetBoneIndex("Armature") != Skeleton::NullIndex && skeleton->GetBoneIndex("Z_UP") != Skeleton::NullIndex);
	CHECK(skeleton->GetParentBoneIndex(0) == Skeleton::NullIndex);
	CHECK(skeleton->GetParentBoneIndex(bone1) == bone);
	CHECK(skeleton->GetParentBoneIndex(bone) == skeleton->GetBoneIndex("Armature"));
	CHECK(Near(skeleton->GetBoneTranslations()[bone1].z, 4.187077f, 1e-4f));  // the file's node translation
	CHECK(Near(skeleton->GetBoneTranslations()[bone].z, -4.18033f, 1e-4f));   // the file's node matrix
	// Every bone has its parent before it, and the inverse really inverts the model-space rest pose.
	for (uint32_t i = 0; i < skeleton->GetNumBones(); i++)
		CHECK(skeleton->GetParentBoneIndex(i) == Skeleton::NullIndex || skeleton->GetParentBoneIndex(i) < i);
	const glm::vec4 probe(1.0f, 2.0f, 3.0f, 1.0f);
	const glm::vec4 roundTrip = skeleton->GetModelSpaceRestPoseInverse()[bone1] * (skeleton->GetModelSpaceRestPose()[bone1] * probe);
	CHECK(Near(roundTrip.x, probe.x, 1e-4f) && Near(roundTrip.y, probe.y, 1e-4f) && Near(roundTrip.z, probe.z, 1e-4f));

	Ref<Animation> clip = clips[0];
	clip->SetSkeletonHandle(Lux::UUID(0x1234abcdULL));
	CHECK(clip->GetTracks().size() == 1);
	CHECK(clip->GetTracks()[0].BoneIndex == bone1);
	CHECK(clip->GetTracks()[0].Translations.size() == 50 && clip->GetTracks()[0].Rotations.size() == 50 && clip->GetTracks()[0].Scales.size() == 50);
	CHECK(Near(clip->GetDuration(), 2.083333f, 1e-4f));
	CHECK(Near(clip->GetTracks()[0].Rotations.front().Time, 0.0416666f, 1e-4f));
	CHECK(Near(glm::length(clip->GetTracks()[0].Rotations[10].Value), 1.0f, 1e-4f));

	// --- skin weights, read the way the mesh importer reads them -----------------------------------
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(model.string(), k_MeshImportFlags);
		CHECK(scene && scene->mNumMeshes > 0);
		if (scene && scene->mNumMeshes > 0)
		{
			std::vector<uint32_t> bases;
			uint32_t total = 0;
			for (uint32_t m = 0; m < scene->mNumMeshes; m++)
			{
				bases.push_back(total);
				total += scene->mMeshes[m]->mNumVertices;
			}
			std::vector<BoneInfluence> influences;
			std::vector<BoneInfo> bones;
			CHECK(ExtractSkinWeights(*scene, bases, total, influences, bones));
			CHECK(influences.size() == total && total > 0);
			CHECK(bones.size() == 2);

			// Every vertex is weighted, to bones the mesh lists, and the weights add up to 1.
			bool allWeighted = true;
			for (const BoneInfluence& influence : influences)
			{
				float sum = 0.0f;
				for (int slot = 0; slot < 4; slot++)
				{
					sum += influence.Weights[slot];
					if (influence.Weights[slot] > 0.0f)
						allWeighted &= influence.BoneInfoIndices[slot] < bones.size();
				}
				allWeighted &= Near(sum, 1.0f, 1e-4f);
			}
			CHECK(allWeighted);

			// Each bone maps to a skeleton bone. The skinning matrix is the bone's model-space pose times its
			// inverse bind pose; at rest it is the transform of the node the inverse binds were authored
			// below, here Z_UP (so a correctly skinned rest pose is just the Z-up to Y-up rotation).
			const glm::mat4& expectedRest = skeleton->GetModelSpaceRestPose()[skeleton->GetBoneIndex("Z_UP")];
			for (const BoneInfo& bone : bones)
			{
				const uint32_t index = skeleton->GetBoneIndex(bone.Name);
				CHECK(index != Skeleton::NullIndex);
				if (index == Skeleton::NullIndex)
					continue;
				const glm::mat4 skinning = skeleton->GetModelSpaceRestPose()[index] * bone.InverseBindPose;
				for (int column = 0; column < 4; column++)
					for (int row = 0; row < 4; row++)
						CHECK(Near(skinning[column][row], expectedRest[column][row], 1e-3f));
			}

			// --- the skin palette (BuildModelPose, BuildSkinPalette) ---------------------------------------
			const auto sameMatrix = [&](const glm::mat4& a, const glm::mat4& b)
			{
				for (int column = 0; column < 4; column++)
					for (int row = 0; row < 4; row++)
						if (!Near(a[column][row], b[column][row], 1e-3f))
							return false;
				return true;
			};

			// At rest: the model pose is the skeleton's own, and every bone's palette matrix is the root
			// transform the inverse binds were authored below.
			const std::vector<glm::mat4> restPose = BuildModelPose(*skeleton);
			bool poseMatches = restPose.size() == skeleton->GetNumBones();
			for (uint32_t i = 0; i < restPose.size() && poseMatches; i++)
				poseMatches = sameMatrix(restPose[i], skeleton->GetModelSpaceRestPose()[i]);
			CHECK(poseMatches);
			uint32_t missing = 99;
			const std::vector<glm::mat4> restPalette = BuildSkinPalette(*skeleton, restPose, bones, &missing);
			CHECK(missing == 0 && restPalette.size() == bones.size());
			for (const glm::mat4& matrix : restPalette)
				CHECK(sameMatrix(matrix, expectedRest));

			// No bend (a zero angle, or no bone) is the rest pose.
			CHECK(sameMatrix(BuildModelPose(*skeleton, bone, 0.0f)[bone1], restPose[bone1]));
			CHECK(sameMatrix(BuildModelPose(*skeleton, Skeleton::NullIndex, 45.0f)[bone1], restPose[bone1]));

			// Bending "Bone" 30 degrees about its local X turns it and its child about the bone's pivot:
			// palette = modelRest(Bone) * Rx * modelRest(Bone)^-1 * (rest palette).
			const glm::mat4 bend = glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			const glm::mat4 expectedBent = restPose[bone] * bend * glm::inverse(restPose[bone]) * expectedRest;
			const std::vector<glm::mat4> bentPalette = BuildSkinPalette(*skeleton, BuildModelPose(*skeleton, bone, 30.0f), bones);
			for (const glm::mat4& matrix : bentPalette)
				CHECK(sameMatrix(matrix, expectedBent));
			CHECK(!sameMatrix(expectedBent, expectedRest)); // the bend really moved something

			// A mesh bone the skeleton lacks gets the identity and is counted.
			std::vector<BoneInfo> withStranger = bones;
			BoneInfo stranger;
			stranger.Name = "NoSuchBone";
			withStranger.push_back(stranger);
			uint32_t strangerMissing = 0;
			const std::vector<glm::mat4> strangerPalette = BuildSkinPalette(*skeleton, restPose, withStranger, &strangerMissing);
			CHECK(strangerMissing == 1 && strangerPalette.size() == bones.size() + 1 && sameMatrix(strangerPalette.back(), glm::mat4(1.0f)));

			// A mesh list that does not match the scene is refused.
			std::vector<BoneInfluence> none;
			std::vector<BoneInfo> noBones;
			CHECK(!ExtractSkinWeights(*scene, {}, total, none, noBones) && none.empty() && noBones.empty());
		}
	}

	// --- round trip through the .lskel / .lanim files --------------------------------------------
	std::filesystem::create_directories(out);
	const auto skelPath = Save(skeleton, out / "RiggedSimple.lskel");
	const auto animPath = Save(clip, out / "Walk.lanim");

	Ref<Skeleton> skeleton2;
	Ref<Animation> clip2;
	CHECK(Load(skeleton2, skelPath));
	CHECK(Load(clip2, animPath));
	if (skeleton2)
		SameSkeleton(*skeleton, *skeleton2);
	if (clip2)
		SameClip(*clip, *clip2);

	// --- malformed blobs are refused and leave the object empty ----------------------------------
	const uint64_t skelSize = std::filesystem::file_size(skelPath);
	const uint64_t animSize = std::filesystem::file_size(animPath);
	for (uint64_t cut : { uint64_t(0), uint64_t(3), skelSize / 2, skelSize - 1 })
	{
		std::filesystem::copy_file(skelPath, out / "cut.lskel", std::filesystem::copy_options::overwrite_existing);
		std::filesystem::resize_file(out / "cut.lskel", cut);
		Ref<Skeleton> bad;
		CHECK(!Load(bad, out / "cut.lskel"));
		CHECK(bad && bad->IsEmpty());
	}
	for (uint64_t cut : { uint64_t(0), uint64_t(3), animSize / 2, animSize - 1 })
	{
		std::filesystem::copy_file(animPath, out / "cut.lanim", std::filesystem::copy_options::overwrite_existing);
		std::filesystem::resize_file(out / "cut.lanim", cut);
		Ref<Animation> bad;
		CHECK(!Load(bad, out / "cut.lanim"));
		CHECK(bad && bad->GetTracks().empty());
	}

	// A skeleton file is not an animation file, and the other way round.
	{
		Ref<Animation> wrong;
		CHECK(!Load(wrong, skelPath));
		Ref<Skeleton> wrong2;
		CHECK(!Load(wrong2, animPath));
	}

	// A file claiming more bones than the limit is refused.
	{
		{
			FileStreamWriter writer(out / "huge.lskel");
			const char magic[4] = { 'L', 'Z', 'S', 'K' };
			writer.WriteData(magic, 4);
			writer.WriteRaw<uint32_t>(1);
			writer.WriteRaw<uint32_t>(0xFFFFFFFFu);
		}
		Ref<Skeleton> huge;
		CHECK(!Load(huge, out / "huge.lskel"));
	}

	// Key times out of order are refused.
	{
		Ref<Animation> unordered = Ref<Animation>::Create();
		unordered->SetDuration(1.0f);
		BoneTrack track;
		track.Rotations = { { 0.5f, glm::quat(1, 0, 0, 0) }, { 0.25f, glm::quat(1, 0, 0, 0) } };
		unordered->GetTracksMutable().push_back(track);
		Save(unordered, out / "unordered.lanim");
		Ref<Animation> loaded;
		CHECK(!Load(loaded, out / "unordered.lanim"));
	}

	CheckFormatsAgree(model.parent_path());

	// A file that cannot be read reports failure and produces nothing.
	{
		Ref<Skeleton> none;
		std::vector<Ref<Animation>> noClips;
		CHECK(!ImportSkeletonAndAnimations(out / "does-not-exist.gltf", none, noClips));
		CHECK(!none && noClips.empty());
	}

	if (g_Failures == 0)
		std::cout << "skeletal import tests: all passed\n";
	else
		std::cerr << "skeletal import tests: " << g_Failures << " failure(s)\n";
	return g_Failures == 0 ? 0 : 1;
}
