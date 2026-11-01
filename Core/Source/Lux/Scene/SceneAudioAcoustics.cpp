// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "lpch.h"
#include "SceneAudioAcoustics.h"
#include "Scene.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/MaterialAsset.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

namespace Lux
{
	namespace
	{
		struct SurfaceCapture
		{
			uint32_t Index = 0, BaseVertex = 0, BaseIndex = 0, IndexCount = 0;
			glm::mat4 Transform{1};
			Soundwave::MaterialTag Material = 0;
		};
		struct MeshCapture : RefCounted
		{
			AudioGeometryInput Description;
			Ref<MeshSource> Mesh;
			std::vector<SurfaceCapture> Surfaces;
			uint32_t ExpectedSurfaces = 0;
		};
		Soundwave::Position Position(glm::vec3 point)
		{
			return {point.x,point.y,point.z};
		}
		Soundwave::MaterialTag Tag(int32_t value)
		{
			return value >= 0 && value < static_cast<int32_t>(Soundwave::kMaterialTagCount) ?
				static_cast<Soundwave::MaterialTag>(value) : 255;
		}
		AudioAcoustics::Builder Builder(std::vector<Ref<MeshCapture>> captures)
		{
			return [captures = std::move(captures)](AudioAcoustics::GeometryInput& output, std::string& error)
			{
				constexpr int boxIndices[] = {0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
					1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3};
				for (const auto& capture : captures)
				{
					const auto& input = capture->Description;
					if (!AudioGeometrySystem::Validate(input))
					{
						error = std::format("invalid transform/material/portal on entity {}", static_cast<uint64_t>(input.Entity));
						return false;
					}
					if (input.Portal)
					{
						if (input.Open >= 1)
							continue;
						if (output.Triangles.size() > Soundwave::Grid::MaximumTriangles - 12)
						{
							error = "Soundwave geometry layer exceeds the triangle limit";
							return false;
						}
						const auto world = AudioGeometrySystem::PortalTransform(input);
						Soundwave::PortalBox portal;
						portal.Instance = input.Entity;
						portal.Material = static_cast<Soundwave::MaterialTag>(input.Material);
						const auto inverse = glm::inverse(world);
						for (uint32_t c = 0; c < 4; ++c)
							for (uint32_t r = 0; r < 4; ++r)
								portal.WorldToBox[c * 4 + r] = inverse[c][r];
						output.Portals.push_back(portal);
						const auto point = [&](int i) { return Position(glm::vec3(world * glm::vec4(i & 1 ? 1 : -1,i & 2 ? 1 : -1,i & 4 ? 1 : -1,1))); };
						const bool reverse = glm::determinant(glm::dmat3(world)) < 0;
						for (int i = 0; i < 36; i += 3)
							output.Triangles.push_back({point(boxIndices[i]),point(boxIndices[i + (reverse ? 2 : 1)]),
								point(boxIndices[i + (reverse ? 1 : 2)]),input.Entity,0,portal.Material,true});
						continue;
					}
					if (!capture->Mesh || capture->Surfaces.empty() || capture->Surfaces.size() != capture->ExpectedSurfaces)
					{
						error = std::format("missing mesh or invalid selected submeshes for asset {} on entity {}", input.Mesh, static_cast<uint64_t>(input.Entity));
						return false;
					}
					const auto& mesh = *capture->Mesh;
					for (const auto& surface : capture->Surfaces)
					{
						const uint64_t first = surface.BaseIndex / 3, end = first + surface.IndexCount / 3;
						if (surface.BaseIndex % 3 || surface.IndexCount % 3 || end > mesh.GetIndices().size())
						{
							error = std::format("invalid submesh {} indices on entity {}", surface.Index, static_cast<uint64_t>(input.Entity));
							return false;
						}
						const auto world = input.Transform * surface.Transform;
						if (end - first > Soundwave::Grid::MaximumTriangles - output.Triangles.size())
						{
							error = "Soundwave geometry layer exceeds the triangle limit";
							return false;
						}
						const bool reverse = glm::determinant(glm::dmat3(world)) < 0;
						for (uint64_t i = first; i < end; ++i)
						{
							const auto& index = mesh.GetIndices()[i];
							std::array<Soundwave::Position, 3> points;
							uint32_t v = 0;
							for (const auto j : {index.V1,index.V2,index.V3})
							{
								const uint64_t vertex = static_cast<uint64_t>(surface.BaseVertex) + j;
								if (vertex >= mesh.GetVertexCount())
								{
									error = std::format("invalid vertex in submesh {} on entity {}", surface.Index, static_cast<uint64_t>(input.Entity));
									return false;
								}
								points[v++] = Position(glm::vec3(world * glm::vec4(mesh.GetVertexPosition(vertex),1)));
							}
							output.Triangles.push_back({points[0],points[reverse ? 2 : 1],points[reverse ? 1 : 2],
								input.Entity,surface.Index,surface.Material,false});
						}
					}
				}
				return true;
			};
		}
	}

	struct SceneAudioAcoustics::Data
	{
		struct Entry
		{
			Ref<MeshCapture> Capture;
			Ref<StaticMesh> Static;
			Ref<MeshSource> Mesh;
			Ref<MaterialTable> Table;
			std::vector<Ref<MaterialAsset>> Materials;
			std::vector<uint32_t> Selected;
			bool Seen = false, AutoMoving = false, FromMaterial = false;
			uint64_t AssetGeneration = 0, TableRevision = 0;
		};
		std::map<uint64_t, Entry> Entries;
		std::vector<uint64_t> Active;
		uint64_t Revision = 0;
		float SinceGeometryRequest = 0, GeometryInterval = 0.05f;
		std::array<bool, 2> Dirty{true,true};
	};

	SceneAudioAcoustics::SceneAudioAcoustics()
		: m_Data(CreateScope<Data>())
	{
	}
	SceneAudioAcoustics::~SceneAudioAcoustics() = default;
	bool SceneAudioAcoustics::Start(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings)
	{
		Clear();
		m_Configured = m_Kernel.Configure(materials, settings);
		if (m_Configured)
			m_Data->GeometryInterval = 1.0f / settings.UpdateRateHz;
		return m_Configured;
	}
	void SceneAudioAcoustics::Sync(Scene& scene)
	{
		if (!m_Configured)
			return;
		LUX_PROFILE_FUNCTION_AUTO;
		m_Kernel.PollGeometry();
		bool dirty[2]{m_Data->Dirty[0],m_Data->Dirty[1]};
		const bool due = !m_Data->Revision || m_Data->SinceGeometryRequest >= m_Data->GeometryInterval;
		for (auto& [id, entry] : m_Data->Entries)
			entry.Seen = false;
		const uint64_t generation = AssetManager::GetDataGeneration();
		const uint64_t tables = MaterialTable::GetGlobalRevision();
		const auto capture = [&](Entity entity, AudioGeometryInput input, const Ref<MaterialTable>& table, bool automatic, bool fromMaterial)
		{
			auto& entry = m_Data->Entries[entity.GetUUID()];
			entry.Seen = true;
			if (!due && entry.Capture)
				return;
			if (entry.Capture && !input.Portal)
			{
				if (automatic && input.Transform != entry.Capture->Description.Transform)
					entry.AutoMoving = true;
				if (automatic && entry.AutoMoving)
					input.Mode = AcousticGeometryMode::Dynamic;
				else if (!automatic && input.Mode == AcousticGeometryMode::Static && entry.Capture->Description.Mode == input.Mode &&
					AudioGeometrySystem::Validate(entry.Capture->Description))
					input.Transform = entry.Capture->Description.Transform;
			}
			bool metadata = !entry.Capture || entry.Capture->Description.Mesh != input.Mesh || entry.Capture->Description.Portal != input.Portal ||
				entry.AssetGeneration != generation || entry.TableRevision != tables || entry.Table != table || entry.FromMaterial != fromMaterial ||
				entry.Capture->Description.Material != input.Material || entry.Capture->Description.Submesh != input.Submesh;
			if (!input.Portal && !metadata && entry.Static && input.Submesh == UINT32_MAX)
				metadata = entry.Selected != entry.Static->GetSubmeshes();
			std::vector<SurfaceCapture> surfaces;
			if (metadata && input.Portal)
			{
				entry.Static = nullptr;
				entry.Mesh = nullptr;
				entry.Selected.clear();
				entry.Materials.clear();
			}
			if (metadata && !input.Portal)
			{
				entry.Static = input.Mesh ? StaticMesh::GetOrCreateRuntime(input.Mesh) : nullptr;
				entry.Mesh = entry.Static ? AssetManager::GetAsset<MeshSource>(entry.Static->GetMeshSource()) : nullptr;
				entry.Materials.clear();
				entry.Selected.clear();
				if (entry.Mesh)
				{
					entry.Selected = entry.Static->GetSubmeshes();
					if (input.Submesh < entry.Mesh->GetSubmeshes().size())
						entry.Selected = {input.Submesh};
					for (auto selected : entry.Selected)
					{
						if (selected >= entry.Mesh->GetSubmeshes().size())
							continue; // rejected below; preserve the invalid selected list for diagnostics
						const auto& submesh = entry.Mesh->GetSubmeshes()[selected];
						Ref<MaterialAsset> material;
						if (fromMaterial)
						{
							const auto handle = ResolveStaticMeshMaterialHandle(table, entry.Static, entry.Mesh, submesh.MaterialIndex);
							if (handle && AssetManager::IsAssetHandleValid(handle) && AssetManager::GetAssetType(handle) == AssetType::Material)
								material = AssetManager::GetAsset<MaterialAsset>(handle);
						}
						entry.Materials.push_back(material);
						surfaces.push_back({selected,submesh.BaseVertex,submesh.BaseIndex,submesh.IndexCount,submesh.Transform,
							Tag(material && material->GetAcousticTag() >= 0 ? material->GetAcousticTag() : static_cast<int32_t>(input.Material))});
					}
				}
			}
			else if (entry.Capture)
			{
				// Live material tags are scalar reads from retained assets; no asset lookup or
				// triangle capture is needed while their authoring remains unchanged.
				for (size_t i = 0; i < entry.Materials.size(); ++i)
				{
					const auto& material = entry.Materials[i];
					const auto tag = Tag(material && material->GetAcousticTag() >= 0 ?
						material->GetAcousticTag() : static_cast<int32_t>(input.Material));
					if (tag != entry.Capture->Surfaces[i].Material)
					{
						if (surfaces.empty())
							surfaces = entry.Capture->Surfaces;
						surfaces[i].Material = tag;
					}
				}
			}
			if (metadata || !surfaces.empty() || !entry.Capture || !(entry.Capture->Description == input))
			{
				if (entry.Capture)
					dirty[entry.Capture->Description.Mode == AcousticGeometryMode::Dynamic ? 1 : 0] = true;
				Ref<MeshCapture> next = Ref<MeshCapture>::Create();
				next->Description = input;
				next->Mesh = entry.Mesh;
			next->ExpectedSurfaces = static_cast<uint32_t>(entry.Selected.size());
				next->Surfaces = !surfaces.empty() || metadata ? std::move(surfaces) : entry.Capture->Surfaces;
				entry.Capture = next;
				dirty[input.Mode == AcousticGeometryMode::Dynamic ? 1 : 0] = true;
			}
			entry.Table = table;
			entry.AssetGeneration = generation;
			entry.TableRevision = tables;
			entry.FromMaterial = fromMaterial;
		};
		for (auto handle : scene.m_Registry.view<TransformComponent, StaticMeshComponent>())
		{
			Entity entity{handle,&scene};
			const auto& mesh = entity.GetComponent<StaticMeshComponent>();
			const auto* collider = entity.TryGetComponent<MeshColliderComponent>();
			const auto* portal = entity.TryGetComponent<AudioPortalComponent>();
			if (!mesh.Visible || !mesh.StaticMesh || scene.IsInactive(handle) || (portal && portal->Enabled) ||
				(collider && collider->AcousticMotion == AcousticGeometryMode::Disabled))
				continue;
			AudioGeometryInput input;
			input.Entity = entity.GetUUID();
			input.Mesh = mesh.StaticMesh;
			input.Submesh = UINT32_MAX;
			input.Mode = collider ? collider->AcousticMotion : AcousticGeometryMode::Static;
			input.Transform = scene.GetWorldSpaceTransformMatrix(entity);
			const auto* surface = entity.TryGetComponent<AudioSurfaceComponent>();
			input.Material = surface ? surface->Material : collider ? collider->Acoustic : AcousticMaterial::Default;
			capture(entity, input, mesh.MaterialTable, !collider, !surface && (!collider || collider->AcousticFromMaterial));
		}
		for (auto handle : scene.m_Registry.view<TransformComponent, AudioPortalComponent>())
		{
			Entity entity{handle,&scene};
			const auto& portal = entity.GetComponent<AudioPortalComponent>();
			if (!portal.Enabled || scene.IsInactive(handle) || portal.Open == 1)
				continue;
			AudioGeometryInput input;
			input.Entity = entity.GetUUID();
			input.Portal = true;
			input.Mode = AcousticGeometryMode::Dynamic;
			input.Transform = scene.GetWorldSpaceTransformMatrix(entity);
			input.HalfExtents = portal.HalfExtents;
			input.Open = portal.Open;
			input.Material = portal.Material;
			capture(entity, input, {}, false, false);
		}
		std::erase_if(m_Data->Entries, [&](const auto& pair)
		{
			if (pair.second.Seen)
				return false;
			if (pair.second.Capture)
				dirty[pair.second.Capture->Description.Mode == AcousticGeometryMode::Dynamic ? 1 : 0] = true;
			return true;
		});
		m_Data->Active.clear();
		std::array<std::vector<Ref<MeshCapture>>, 2> captures;
		for (const auto& [id, entry] : m_Data->Entries)
		{
			m_Data->Active.push_back(id);
			const size_t layer = entry.Capture->Description.Mode == AcousticGeometryMode::Dynamic ? 1 : 0;
			if (due && dirty[layer])
				captures[layer].push_back(entry.Capture);
		}
		if (!m_Kernel.SetActiveInstances(m_Data->Active))
			return; // adapter reports the concrete error
		m_Data->Dirty = {dirty[0],dirty[1]};
		if (due && (dirty[0] || dirty[1]))
		{
			if (!m_Kernel.RequestGeometry(++m_Data->Revision, dirty[0] ? Builder(std::move(captures[0])) : AudioAcoustics::Builder{},
				dirty[1] ? Builder(std::move(captures[1])) : AudioAcoustics::Builder{}))
				return;
			m_Data->Dirty = {};
			m_Data->SinceGeometryRequest = 0;
		}
		m_Kernel.PollGeometry();
	}
	void SceneAudioAcoustics::Update(Scene& scene, float timestep, Soundwave::Position listener)
	{
		if (!m_Configured)
			return;
		if (std::isfinite(timestep) && timestep >= 0)
			m_Data->SinceGeometryRequest = std::min(m_Data->SinceGeometryRequest + timestep, m_Data->GeometryInterval);
		m_Sources.clear();
		for (auto handle : scene.m_Registry.view<TransformComponent, AudioSourceComponent>())
		{
			Entity entity{handle,&scene};
			const auto& source = entity.GetComponent<AudioSourceComponent>();
			if (source.Event.IsValid() && source.Enabled && !scene.IsInactive(handle) && !scene.IsAudioSourceCulled(entity.GetUUID()))
				m_Sources.push_back({entity.GetUUID(),Position(glm::vec3(scene.GetWorldSpaceTransformMatrix(entity)[3]))});
		}
		if (!m_Kernel.Update(timestep, listener, m_Sources))
			return;
	}
	void SceneAudioAcoustics::Clear()
	{
		m_Kernel.Clear();
		m_Data->Entries.clear();
		m_Data->Active.clear();
		m_Data->Revision = 0;
		m_Data->SinceGeometryRequest = 0;
		m_Data->Dirty = {true,true};
		m_Sources.clear();
		m_Configured = false;
	}
}
