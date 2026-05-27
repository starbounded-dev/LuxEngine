#include "lpch.h"
#include "TextureSerializer.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/SceneEnvironment.h"
#include "Lux/Serialization/TextureRuntimeSerializer.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"

#include <filesystem>

namespace Lux
{
	namespace
	{
		bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& directory)
		{
			const std::filesystem::path normalizedPath = path.lexically_normal();
			const std::filesystem::path normalizedDirectory = directory.lexically_normal();

			auto pathIt = normalizedPath.begin();
			for (auto directoryIt = normalizedDirectory.begin(); directoryIt != normalizedDirectory.end(); ++directoryIt, ++pathIt)
			{
				if (pathIt == normalizedPath.end() || *pathIt != *directoryIt)
					return false;
			}

			return true;
		}

		bool IsMeshSourceTexture(const std::filesystem::path& assetPath)
		{
			Ref<Project> project = Project::GetActive();
			if (!project)
				return false;

			const std::filesystem::path meshSourcePath = project->GetConfig().MeshSourcePath;
			if (meshSourcePath.empty())
				return false;

			return PathStartsWith(assetPath, meshSourcePath);
		}
	}
	void TextureSerializer::Serialize(const AssetMetadata& metadata, const Ref<Asset>& asset) const
	{
		// Source-format textures (.png/.jpg/…) are their own serialized form – nothing to write back.
	}

	bool TextureSerializer::TryLoadData(const AssetMetadata& metadata, Ref<Asset>& asset) const
	{
		const std::filesystem::path filepath = Project::GetEditorAssetManager()
			? Project::GetEditorAssetManager()->GetFileSystemPath(metadata)
			: (Project::GetActiveAssetDirectory() / metadata.FilePath);

		if (metadata.Type == AssetType::EnvMap)
		{
			if (!std::filesystem::exists(filepath))
				return false;

			auto [radianceMap, irradianceMap] = Renderer::CreateEnvironmentMap(filepath.string());
			if (!radianceMap || !irradianceMap)
				return false;

			asset = Ref<Environment>::Create(radianceMap, irradianceMap);
			if (asset)
				asset->Handle = metadata.Handle;
			return asset != nullptr;
		}

		TextureSpecification spec;
		if (IsMeshSourceTexture(metadata.FilePath))
		{
			spec.FlipVertically = false;
			spec.MaxAnisotropy = 16.0f;
		}
		Ref<Texture2D> texture = Texture2D::Create(spec, filepath);
		if (!texture || !texture->Loaded())
			return false;

		texture->Handle = metadata.Handle;
		asset = texture;
		return true;
	}

	bool TextureSerializer::SerializeToAssetPack(AssetHandle handle, FileStreamWriter& stream, AssetSerializationInfo& outInfo) const
	{
		outInfo.Offset = stream.GetStreamPosition();

		if (AssetManager::GetAssetType(handle) == AssetType::EnvMap)
		{
			auto assetManager = Project::GetEditorAssetManager();
			if (!assetManager)
				return false;

			const AssetMetadata metadata = assetManager->GetMetadata(handle);
			Buffer sourceBuffer = FileSystem::ReadBytes(assetManager->GetFileSystemPath(metadata));
			if (!sourceBuffer)
				return false;

			stream.WriteBuffer(sourceBuffer);
			sourceBuffer.Release();
			outInfo.Size = stream.GetStreamPosition() - outInfo.Offset;
			return true;
		}

		Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
		if (!texture)
			return false;

		outInfo.Size = TextureRuntimeSerializer::SerializeTexture2DToFile(texture, stream);
		return true;
	}

	Ref<Asset> TextureSerializer::DeserializeFromAssetPack(FileStreamReader& stream, const AssetPackFile::AssetInfo& assetInfo) const
	{
		stream.SetStreamPosition(assetInfo.PackedOffset);

		const AssetType assetType = (AssetType)assetInfo.Type;
		if (assetType == AssetType::EnvMap)
		{
			Buffer sourceBuffer;
			stream.ReadBuffer(sourceBuffer);
			if (!sourceBuffer)
				return nullptr;

			TextureSpecification equirectSpec;
			equirectSpec.DebugName = "PackedEnvEquirect";
			equirectSpec.FlipVertically = false;
			equirectSpec.Width = (uint32_t)sourceBuffer.Size;
			equirectSpec.Height = 0;

			Ref<Texture2D> equirectTexture = Texture2D::Create(equirectSpec, sourceBuffer);
			sourceBuffer.Release();

			if (!equirectTexture || !equirectTexture->Loaded())
				return nullptr;

			auto [radiance, irradiance] = Renderer::CreateEnvironmentMap(equirectTexture);
			if (!radiance || !irradiance)
				return nullptr;

			return Ref<Environment>::Create(radiance, irradiance);
		}

		return TextureRuntimeSerializer::DeserializeTexture2D(stream);
	}
}
