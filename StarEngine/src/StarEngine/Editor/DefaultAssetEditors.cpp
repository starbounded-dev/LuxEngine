#include "sepch.h"
#include "DefaultAssetEditors.h"
#include "StarEngine/Asset/AssetImporter.h"
#include "StarEngine/Asset/AssetManager.h"
//#include "StarEngine/Audio/AudioFileUtils.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Editor/SelectionManager.h"
#include "StarEngine/Audio/AudioEngine.h"
//#include "StarEngine/Audio/Editor/SoundConfigProperties.h"
//#include "StarEngine/Editor/Properties/GenericPropertyEdit.h"

#include "imgui_internal.h"

namespace StarEngine {
	/*
	MaterialEditor::MaterialEditor()
		: AssetEditor("Material Editor")
	{
	}

	void MaterialEditor::OnOpen()
	{
		if (!m_MaterialAsset)
			SetOpen(false);
	}

	void MaterialEditor::OnClose()
	{
		m_MaterialAsset = nullptr;
	}

	void MaterialEditor::Render()
	{
		HZ_CORE_ASSERT(m_MaterialAsset);

		bool needsSerialize = false;

		bool transparent = m_MaterialAsset->IsTransparent();
		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::PushID();
		if (ImGuiEx::Property("Transparent", transparent))
		{
			if (transparent)
				m_MaterialAsset->SetMaterial(Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Transparent")));
			else
				m_MaterialAsset->SetMaterial(Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Static")));

			m_MaterialAsset->m_Transparent = transparent;
			m_MaterialAsset->SetDefaults();
			needsSerialize = true;
		}
		ImGuiEx::PopID();
		ImGuiEx::EndPropertyGrid();

		ImGui::Text("Shader: %s", m_MaterialAsset->GetMaterial()->GetShader()->GetName().c_str());
		
		auto getDroppedTextureHandle = []() {
			auto data = ImGui::AcceptDragDropPayload("asset_payload"); 
			if (data && data->DataSize / sizeof(AssetHandle) == 1)
			{
				AssetHandle assetHandle = *(((AssetHandle*)data->Data));
				Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
				if (asset && asset->GetAssetType() == AssetType::Texture)
				{
					return asset->Handle;
				}
			}
			return AssetHandle{ 0 };
		};

		// Albedo
		if (ImGui::CollapsingHeader("Albedo", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

			auto albedoColor = m_MaterialAsset->GetAlbedoColor();
			auto albedoMap = m_MaterialAsset->GetAlbedoMap();
			bool hasAlbedoMap = albedoMap ? !albedoMap.EqualsObject(Renderer::GetWhiteTexture()) && albedoMap->GetImage() : false;
			Ref<Texture2D> albedoUITexture = hasAlbedoMap ? albedoMap : EditorResources::CheckerboardTexture;

			ImVec2 textureCursorPos = ImGui::GetCursorPos();
			ImGuiEx::Image(albedoUITexture, ImVec2(64, 64));
			if (ImGui::BeginDragDropTarget())
			{
				if (auto handle = getDroppedTextureHandle(); handle != 0)
				{
					m_MaterialAsset->SetAlbedoMap(handle);
					needsSerialize = true;
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::PopStyleVar();
			if (ImGui::IsItemHovered())
			{
				if (hasAlbedoMap)
				{
					ImGuiEx::ImageToolTip(albedoMap);
				}
				if (ImGui::IsItemClicked())
				{
				}
			}

			ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
			ImGui::SameLine();
			ImVec2 properCursorPos = ImGui::GetCursorPos();
			ImGui::SetCursorPos(textureCursorPos);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
			if (hasAlbedoMap && ImGui::Button("X##AlbedoMap", ImVec2(18, 18)))
			{
				m_MaterialAsset->ClearAlbedoMap();
				needsSerialize = true;
			}
			ImGui::PopStyleVar();
			ImGui::SetCursorPos(properCursorPos);

			if (ImGuiEx::ColorEdit3("##Albedo", glm::value_ptr(albedoColor), ImGuiColorEditFlags_NoInputs))
				m_MaterialAsset->SetAlbedoColor(albedoColor);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			ImGui::SameLine();
			ImGui::TextUnformatted("Color");

			float& emissive = m_MaterialAsset->GetEmission();
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			ImGuiEx::DragFloat("##Emission", &emissive, 0.1f, 0.0f, 20.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			ImGui::SameLine();
			ImGui::TextUnformatted("Emission");

			ImGui::SetCursorPos(nextRowCursorPos);
		}

		if (transparent)
		{
			float& transparency = m_MaterialAsset->GetTransparency();

			ImGuiEx::BeginPropertyGrid();
			ImGuiEx::Property("Transparency", transparency, 0.01f, 0.0f, 1.0f);
			if (ImGui::IsItemDeactivated())
				needsSerialize = true;
			ImGuiEx::EndPropertyGrid();
		}
		else
		{
			// Textures ------------------------------------------------------------------------------
			{
				// Normals
				if (ImGui::CollapsingHeader("Normals", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					bool useNormalMap = m_MaterialAsset->IsUsingNormalMap();
					Ref<Texture2D> normalMap = m_MaterialAsset->GetNormalMap();

					bool hasNormalMap = normalMap ? !normalMap.EqualsObject(Renderer::GetWhiteTexture()) && normalMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					ImGuiEx::Image(hasNormalMap ? normalMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetNormalMap(handle);
							m_MaterialAsset->SetUseNormalMap(true);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasNormalMap)
						{
							ImGuiEx::ImageToolTip(normalMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasNormalMap && ImGui::Button("X##NormalMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearNormalMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);

					if (ImGuiEx::Checkbox("##Use", &useNormalMap))
						m_MaterialAsset->SetUseNormalMap(useNormalMap);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Use");

					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}
			{
				// Metalness
				if (ImGui::CollapsingHeader("Metalness", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					float& metalnessValue = m_MaterialAsset->GetMetalness();
					Ref<Texture2D> metalnessMap = m_MaterialAsset->GetMetalnessMap();

					bool hasMetalnessMap = metalnessMap ? !metalnessMap.EqualsObject(Renderer::GetWhiteTexture()) && metalnessMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					ImGuiEx::Image(hasMetalnessMap ? metalnessMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetMetalnessMap(handle);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasMetalnessMap)
						{
							ImGuiEx::ImageToolTip(metalnessMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasMetalnessMap && ImGui::Button("X##MetalnessMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearMetalnessMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);
					ImGui::SetNextItemWidth(200.0f);
					ImGuiEx::SliderFloat("##MetalnessInput", &metalnessValue, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Metalness Value");
					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}
			{
				// Roughness
				if (ImGui::CollapsingHeader("Roughness", nullptr, ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
					float& roughnessValue = m_MaterialAsset->GetRoughness();
					Ref<Texture2D> roughnessMap = m_MaterialAsset->GetRoughnessMap();
					bool hasRoughnessMap = roughnessMap ? !roughnessMap.EqualsObject(Renderer::GetWhiteTexture()) && roughnessMap->GetImage() : false;
					ImVec2 textureCursorPos = ImGui::GetCursorPos();
					ImGuiEx::Image(hasRoughnessMap ? roughnessMap : EditorResources::CheckerboardTexture, ImVec2(64, 64));

					if (ImGui::BeginDragDropTarget())
					{
						if (auto handle = getDroppedTextureHandle(); handle != 0)
						{
							m_MaterialAsset->SetRoughnessMap(handle);
							needsSerialize = true;
						}
						ImGui::EndDragDropTarget();
					}

					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered())
					{
						if (hasRoughnessMap)
						{
							ImGuiEx::ImageToolTip(roughnessMap);
						}
						if (ImGui::IsItemClicked())
						{
						}
					}
					ImVec2 nextRowCursorPos = ImGui::GetCursorPos();
					ImGui::SameLine();
					ImVec2 properCursorPos = ImGui::GetCursorPos();
					ImGui::SetCursorPos(textureCursorPos);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					if (hasRoughnessMap && ImGui::Button("X##RoughnessMap", ImVec2(18, 18)))
					{
						m_MaterialAsset->ClearRoughnessMap();
						needsSerialize = true;
					}
					ImGui::PopStyleVar();
					ImGui::SetCursorPos(properCursorPos);
					ImGui::SetNextItemWidth(200.0f);
					ImGuiEx::SliderFloat("##RoughnessInput", &roughnessValue, 0.0f, 1.0f);
					if (ImGui::IsItemDeactivated())
						needsSerialize = true;
					ImGui::SameLine();
					ImGui::TextUnformatted("Roughness Value");
					ImGui::SetCursorPos(nextRowCursorPos);
				}
			}

			ImGuiEx::BeginPropertyGrid();

			bool castsShadows = m_MaterialAsset->IsShadowCasting();
			if (ImGuiEx::Property("Casts shadows", castsShadows))
				m_MaterialAsset->SetShadowCasting(castsShadows);

			ImGuiEx::EndPropertyGrid();
		}

		if (needsSerialize && !AssetManager::IsMemoryAsset(m_MaterialAsset->Handle))
			AssetImporter::Serialize(m_MaterialAsset);
	}
	*/
	TextureViewer::TextureViewer()
		: AssetEditor("Edit Texture")
	{
		SetMinSize(200, 600);
		SetMaxSize(500, 1000);
	}

	void TextureViewer::OnOpen()
	{
		if (!m_Asset)
			SetOpen(false);
	}

	void TextureViewer::OnClose()
	{
		m_Asset = nullptr;
	}

	void TextureViewer::Render()
	{
		float textureWidth = (float)m_Asset->GetWidth();
		float textureHeight = (float)m_Asset->GetHeight();
		//float bitsPerPixel = Texture::GetBPP(m_Asset->GetFormat());
		float imageSize = ImGui::GetWindowWidth() - 40;
		imageSize = glm::min(imageSize, 500.0f);

		ImGui::SetCursorPosX(20);
		ImGuiEx::Image(m_Asset.As<Texture2D>(), {imageSize, imageSize});

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::BeginDisabled();
		ImGuiEx::Property("Width", textureWidth);
		ImGuiEx::Property("Height", textureHeight);
		// UI::Property("Bits", bitsPerPixel, 0.1f, 0.0f, 0.0f, true); // TODO: Format
		ImGuiEx::EndDisabled();
		ImGuiEx::EndPropertyGrid();
	}
#if 0 
	AudioFileViewer::AudioFileViewer()
		: AssetEditor("Audio File")
	{
		SetMinSize(340, 340);
		SetMaxSize(500, 500);
	}

	void AudioFileViewer::SetAsset(const Ref<Asset>& asset)
	{
		m_Asset = (Ref<AudioFile>)asset;

		const AssetMetadata& metadata = Project::GetEditorAssetManager()->GetMetadata(m_Asset.As<AudioFile>()->Handle);
		SetTitle(metadata.FilePath.stem().string());
	}

	void AudioFileViewer::OnOpen()
	{
		if (!m_Asset)
			SetOpen(false);
	}

	void AudioFileViewer::OnClose()
	{
		m_Asset = nullptr;
	}

	void AudioFileViewer::Render()
	{
		std::chrono::duration<double> seconds{ m_Asset->Duration };
		auto duration = Utils::DurationToString(seconds);

		auto samplingRate = std::to_string(m_Asset->SamplingRate) + " Hz";
		auto bitDepth = std::to_string(m_Asset->BitDepth) + "-bit";
		auto channels = AudioFileUtils::ChannelsToLayoutString(m_Asset->NumChannels);
		auto fileSize = Utils::BytesToString(m_Asset->FileSize);
		auto filePath = Project::GetEditorAssetManager()->GetMetadata(m_Asset->Handle).FilePath.string();

		auto localBounds = ImGui::GetContentRegionAvail();
		localBounds.y -= 14.0f; // making sure to not overlap resize corner

		// Hacky way to hide Property widget background, while making overall text contrast more pleasant
		auto& colors = ImGui::GetStyle().Colors;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, colors[ImGuiCol_FrameBg]);

		ImGui::BeginChild("AudioFileProps", localBounds, false);
		ImGui::Spacing();

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Duration", duration.c_str());
		ImGuiEx::Property("Sampling rate", samplingRate.c_str());
		ImGuiEx::Property("Bit depth", bitDepth.c_str());
		ImGuiEx::Property("Channels", channels.c_str());
		ImGuiEx::Property("File size", fileSize.c_str());
		ImGuiEx::Property("File path", filePath.c_str());
		ImGuiEx::EndPropertyGrid();

		ImGui::EndChild();

		ImGui::PopStyleColor(); // ImGuiCol_ChildBg
	}

	SoundConfigEditor::SoundConfigEditor()
		: AssetEditor("Sound Configuration")
	{
		SetMinSize(340, 340);
		SetMaxSize(700, 700);
	}

	void SoundConfigEditor::SetAsset(const Ref<Asset>& asset)
	{
		MiniAudioEngine::Get().StopActiveAudioFilePreview();

		m_Asset = (Ref<SoundConfig>)asset;

		const AssetMetadata& metadata = Project::GetEditorAssetManager()->GetMetadata(m_Asset.As<SoundConfig>()->Handle);
		SetTitle(metadata.FilePath.stem().string());
	}

	void SoundConfigEditor::OnOpen()
	{
		MiniAudioEngine::Get().StopActiveAudioFilePreview();

		if (!m_Asset)
			SetOpen(false);
		else if (m_Asset->Spatialization)
			m_Edit.reset(new Properties::SpatializationConfigParameterEdit{ *m_Asset->Spatialization });
		else
			m_Edit.reset();

	}

	void SoundConfigEditor::OnClose()
	{
		MiniAudioEngine::Get().StopActiveAudioFilePreview();

		if (m_Asset)
			AssetImporter::Serialize(m_Asset);

		m_Edit.reset();
		m_Asset = nullptr;
	}

	void SoundConfigEditor::Render()
	{
		auto localBounds = ImGui::GetContentRegionAvail();
		localBounds.y -= 14.0f; // making sure to not overlap resize corner

		ImGui::BeginChild("SoundConfigPanel", localBounds, false);
		ImGui::Spacing();

		{
			// PropertyGrid consists out of 2 columns, so need to move cursor accordingly
			auto propertyGridSpacing = []
			{
				ImGui::Spacing();
				ImGui::NextColumn();
				ImGui::NextColumn();
			};

			//=======================================================

			SoundConfig& soundConfig = *m_Asset;

			// Adding space after header
			ImGui::Spacing();

			//--- Sound Assets and Looping
			//----------------------------
			ImGuiEx::PushID();
			ImGuiEx::BeginPropertyGrid();
			{
				// Need to wrap this first Property Grid into another ID,
				// otherwise there's a conflict with the next Property Grid.

				{
					ImGuiEx::PropertyAssetReferenceError error;
					ImGuiEx::PropertyAssetReferenceSettings settings;
					settings.ShowFullFilePath = false;
					ImGuiEx::PropertyMultiAssetReference<AssetType::Audio, AssetType::SoundGraphSound>("Source", soundConfig.DataSourceAsset, "", &error, settings);
				}

				if (soundConfig.DataSourceAsset)
				{
					const auto path = Project::GetEditorAssetManager()->GetFileSystemPath(soundConfig.DataSourceAsset);
					if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path))
					{
						const bool isValidAudioFile = AudioFileUtils::IsValidAudioFile(path.string());

						ImGuiEx::BeginDisabled(!isValidAudioFile);

						ImGui::NextColumn();

						if (ImGui::Button("Preview"))
						{
							MiniAudioEngine::Get().PreviewAudioFile(soundConfig.DataSourceAsset);
						}

						ImGui::SameLine();

						if (ImGui::Button("Stop"))
						{
							MiniAudioEngine::Get().StopActiveAudioFilePreview();
						}

						ImGui::NextColumn();

						ImGuiEx::EndDisabled();
					}
				}

				propertyGridSpacing();

				ImGuiEx::Property("Volume Multiplier", soundConfig.VolumeMultiplier, 0.01f, 0.0f, 1.0f); //TODO: switch to dBs in the future ?
				ImGuiEx::Property("Pitch Multiplier", soundConfig.PitchMultiplier, 0.01f, 0.0f, 24.0f); // max pitch 24 is just an arbitrary number here

				propertyGridSpacing();
				propertyGridSpacing();

				ImGuiEx::Property("Looping", soundConfig.bLooping);

				propertyGridSpacing();
				propertyGridSpacing();

				// Currently we use normalized value for the filter properties,
				// which are then translated to frequency scale

				//? was trying to make a more usable logarithmic scale for the slider value,
				//? so that 0.5 represents 1.2 kHz, or 0.0625 of frequency range normalized
				auto logFrequencyToNormScale = [](float frequencyN)
				{
					return (log2(frequencyN) + 8.0f) / 8.0f;
				};
				auto sliderScaleToLogFreq = [](float sliderValue)
				{
					return pow(2.0f, 8.0f * sliderValue - 8.0f);
				};

				//float lpfFreq = logFrequencyToNormScale(1.0f - soundConfig.LPFilterValue * 0.99609375f);
				float lpfFreq = /*1.0f -*/ soundConfig.LPFilterValue;
				if (ImGuiEx::Property("Low-Pass Filter", lpfFreq, 0.0f, 0.0f, 1.0f))
				{
					lpfFreq = std::clamp(lpfFreq, 0.0f, 1.0f);
					//soundConfig.LPFilterValue = sliderScaleToLogFreq(lpfFreq);
					soundConfig.LPFilterValue = /*1.0f - */lpfFreq;
				}

				//float hpfFreq = logFrequencyToNormScale(soundConfig.HPFilterValue * 0.99609375f + 0.00390625f);
				float hpfFreq = soundConfig.HPFilterValue;
				if (ImGuiEx::Property("High-Pass Filter", hpfFreq, 0.0f, 0.0f, 1.0f))
				{
					hpfFreq = std::clamp(hpfFreq, 0.0f, 1.0f);
					//soundConfig.HPFilterValue = sliderScaleToLogFreq(1.0f - hpfFreq);
					soundConfig.HPFilterValue = hpfFreq;
				}

				propertyGridSpacing();
				propertyGridSpacing();

				ImGuiEx::Property("Master Reverb send", soundConfig.MasterReverbSend, 0.01f, 0.0f, 1.0f);
			}

			ImGuiEx::EndPropertyGrid();
			ImGuiEx::PopID();

			ImGui::Spacing();
			ImGui::Spacing();

			auto contentRegionAvailable = ImGui::GetContentRegionAvail();

			//--- Enable Spatialization
			//-------------------------
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 4.0f, 6.0f });
			bool spatializationOpen = ImGuiEx::PropertyGridHeader("Spatialization", true);
			ImGui::PopStyleVar(); // ImGuiStyleVar_FramePadding
			ImGui::SameLine(contentRegionAvailable.x - (ImGui::GetFrameHeight() + GImGui->Style.FramePadding.y * 2.0f));
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
			ImGuiEx::Checkbox("##enabled", &soundConfig.bSpatializationEnabled);

			ImGui::Indent(12.0f);

			//--- Spatialization Settings
			//---------------------------
			if (spatializationOpen && m_Edit)
			{
				ImGui::Spacing();

				m_Edit->Edit->Options.Set(Properties::EEditGroupOptions::InlineInner);
				m_Edit->DisplayEdit();
				ImGui::TreePop();
			}

			ImGui::Unindent();
		}

		ImGui::EndChild(); // SoundConfigPanel
	}

	SpatializationConfigEditor::SpatializationConfigEditor()
		: AssetEditor("Spatialization Configuration")
	{
		SetMinSize(340, 340);
		SetMaxSize(700, 700);
	}

	void SpatializationConfigEditor::SetAsset(const Ref<Asset>& asset)
	{
		m_Asset = asset.As<SpatializationConfig>();

		const AssetMetadata& metadata = Project::GetEditorAssetManager()->GetMetadata(m_Asset.As<SpatializationConfig>()->Handle);
		SetTitle(metadata.FilePath.stem().string());
	}

	void SpatializationConfigEditor::OnOpen()
	{
		if (!m_Asset)
			SetOpen(false);
		else
			m_Edit.reset(new Properties::SpatializationConfigParameterEdit{ *m_Asset });
	}

	void SpatializationConfigEditor::OnClose()
	{
		if (m_Asset)
			AssetImporter::Serialize(m_Asset);

		m_Asset = nullptr;
		m_Edit.reset();
	}

	void SpatializationConfigEditor::Render()
	{
		if (!m_Asset || !m_Edit) [[unlikely]]
			return;

		if (m_Edit->DisplayEdit())
		{
			// NOTE: JP. serialzing on each edit is way too heavy
			//AssetImporter::Serialize(m_Asset);
		}
	}

	PrefabEditor::PrefabEditor()
		: AssetEditor("Prefab Editor"), m_SceneHierarchyPanel(nullptr, false)
	{
	}

	void PrefabEditor::OnOpen()
	{
		SelectionManager::DeselectAll();
	}

	void PrefabEditor::OnClose()
	{
		SelectionManager::DeselectAll();
	}

	void PrefabEditor::Render()
	{
		// Need to do this in order to ensure that the scene hierarchy panel doesn't close immediately.
		// There's been some structural changes since the addition of the PanelManager.
		bool isOpen = true;
		m_SceneHierarchyPanel.OnImGuiRender(isOpen);
	}
#endif
}
