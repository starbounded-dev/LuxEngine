// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialGraphDocument.h"

#include "MaterialGraphCanvas.h"

#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"

namespace Lux
{
	MaterialGraphDocument::MaterialGraphDocument(const Ref<MaterialAsset>& material, const std::filesystem::path& path)
		: GraphDocument(material->Handle, AssetType::Material), Saved(MaterialState::Capture(material)),
		  EditBaseline(Saved), Propagated(Saved), SavedGraph(GraphSnapshot(material))
	{
		RefreshLabel(path);
	}

	MaterialGraphDocument::MaterialState MaterialGraphDocument::MaterialState::Capture(Ref<MaterialAsset> material)
	{
		MaterialState state;
		state.AlbedoColor = material->GetAlbedoColor();
		state.Metalness = material->GetMetalness();
		state.Roughness = material->GetRoughness();
		state.Emission = material->GetEmission();
		state.Transparency = material->GetTransparency();
		state.CastShadows = material->IsShadowCasting();
		state.UseNormalMap = material->IsUsingNormalMap();
		state.AlbedoMap = material->GetAlbedoMapHandle();
		state.NormalMap = material->GetNormalMapHandle();
		state.MetalnessMap = material->GetMetalnessMapHandle();
		state.RoughnessMap = material->GetRoughnessMapHandle();
		state.Surface = material->GetSurfaceParameters();
		state.AcousticTag = material->GetAcousticTag();
		state.Parent = material->GetParent();
		state.Overrides = material->GetOverrides();
		state.GraphParameters = material->GetGraphParameters();
		return state;
	}

	void MaterialGraphDocument::MaterialState::Apply(Ref<MaterialAsset> material) const
	{
		material->SetAlbedoColor(AlbedoColor);
		material->SetMetalness(Metalness);
		material->SetRoughness(Roughness);
		material->SetEmission(Emission);
		material->SetTransparency(Transparency);
		material->SetShadowCasting(CastShadows);

		if (AlbedoMap) material->SetAlbedoMap(AlbedoMap); else material->ClearAlbedoMap();
		if (NormalMap) material->SetNormalMap(NormalMap); else material->ClearNormalMap();
		if (MetalnessMap) material->SetMetalnessMap(MetalnessMap); else material->ClearMetalnessMap();
		if (RoughnessMap) material->SetRoughnessMap(RoughnessMap); else material->ClearRoughnessMap();

		// After the maps: assigning or clearing a normal map may change the flag.
		material->SetUseNormalMap(UseNormalMap);
		material->SetSurfaceParameters(Surface);
		material->SetAcousticTag(AcousticTag);
		material->SetParent(Parent);
		material->SetOverrides(Overrides);
		material->SetGraphParameters(GraphParameters);
	}

	std::string MaterialGraphDocument::GraphSnapshot(const Ref<MaterialAsset>& material)
	{
		Ref<MaterialGraph> graph = material ? material->GetGraph() : nullptr;
		return graph && !material->GetParent() ? MaterialGraphCanvas::Snapshot(*graph) : std::string();
	}

	void MaterialGraphDocument::ApplyGraphSnapshot(AssetHandle handle, const std::string& snapshot)
	{
		if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(handle))
		{
			if (material->GetParent())
			{
				if (Ref<MaterialAsset> parent =
						AssetManager::GetAsset<MaterialAsset>(material->GetParent()))
					material->InheritFrom(*parent);
				MaterialGraphRegistry::Unregister(handle);
				return;
			}
			if (snapshot.empty())
			{
				material->SetGraph(nullptr);
				MaterialGraphRegistry::Unregister(handle);
			}
			else
			{
				Ref<MaterialGraph> graph = MaterialGraph::CreateDefault();
				MaterialGraphCanvas::Restore(*graph, snapshot);
				material->SetGraph(graph);
				MaterialGraphRegistry::Register(handle);
			}
			material->MarkChanged();
			// Instances share the parent's graph object: a replaced one leaves them on a graph nothing compiles.
			MaterialAsset::PropagateToInstances(handle);
			MaterialGraphRegistry::MarkDirty();
		}
	}

	bool MaterialGraphDocument::Save()
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(Handle);
		if (!material)
		{
			LUX_CORE_ERROR_TAG("Editor", "Material Editor: cannot save material {}: it is no longer loaded", (uint64_t)Handle);
			SaveError = "The material is no longer loaded. Your unsaved document remains open.";
			return false;
		}

		if (!AssetImporter::TrySerialize(material.As<Asset>()))
		{
			SaveError = "Could not write the material. Check that its folder is writable, then "
								 "retry Save. Your changes remain available.";
			return false;
		}
		Saved = MaterialState::Capture(material);
		SavedGraph = GraphSnapshot(material);
		SaveError.clear();
		return true;
	}

	bool MaterialGraphDocument::IsDirty() const
	{
		Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(Handle);
		return material && (!(MaterialState::Capture(material) == Saved) || GraphSnapshot(material) != SavedGraph);
	}

	void MaterialGraphDocument::Discard()
	{
		if (Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(Handle))
		{
			Saved.Apply(material);
			ApplyGraphSnapshot(Handle, SavedGraph);
			MaterialAsset::PropagateToInstances(Handle);
		}
	}
}
