#include "lpch.h"
#include "ImGuiEx.h"

#include "ImGuiUtilities.h"
//#include "Lux/Script/ScriptEngine.h"

#include <imgui_internal.h>

#include <format>

namespace Lux::ImGuiEx {

	static int s_UIContextID = 0;
	static uint32_t s_Counter = 0;
	static char s_IDBuffer[16 + 2 + 1] = "##";
	static char s_LabelIDBuffer[1024 + 1];

	const char* GenerateID()
	{
		snprintf(s_IDBuffer + 2, 16, "%u", s_Counter++);
		return s_IDBuffer;
	}

	const char* GenerateLabelID(std::string_view label)
	{
		*std::format_to_n(s_LabelIDBuffer, std::size(s_LabelIDBuffer), "{}##{}", label, s_Counter++).out = 0;
		return s_LabelIDBuffer;
	}

	void PushID()
	{
		ImGui::PushID(s_UIContextID++);
		s_Counter = 0;
	}

	void PopID()
	{
		ImGui::PopID();
		s_UIContextID--;
	}

	bool IsInputEnabled()
	{
		const auto& io = ImGui::GetIO();
		return (io.ConfigFlags & ImGuiConfigFlags_NoMouse) == 0 && (io.ConfigFlags & ImGuiConfigFlags_NavNoCaptureKeyboard) == 0;
	}

	void SetInputEnabled(bool enabled)
	{
		auto& io = ImGui::GetIO();

		if (enabled)
		{
			io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
			io.ConfigFlags &= ~ImGuiConfigFlags_NavNoCaptureKeyboard;
		}
		else
		{
			io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
			io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
		}
	}

	void ShiftCursorX(float distance)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + distance);
	}

	void ShiftCursorY(float distance)
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + distance);
	}

	void ShiftCursor(float x, float y)
	{
		const ImVec2 cursor = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(cursor.x + x, cursor.y + y));
	}

	void BeginPropertyGrid(uint32_t columns)
	{
		PushID();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
		ImGui::Columns(columns);
	}

	void EndPropertyGrid()
	{
		ImGui::Columns(1);
		Draw::Underline();
		ImGui::PopStyleVar(2); // ItemSpacing, FramePadding
		ShiftCursorY(18.0f);
		ImGui::Dummy({ 0.0f,0.0f }); // From ImGui 1.89, extending content size via SetCursorPos (which ShiftCursorY() calls), requires submission of an item. Refer ImGui::ErrorCheckUsingSetCursorPosToExtendParentBoundaries()

		PopID();
	}

	bool BeginTreeNode(const char* name, bool defaultOpen)
	{
		ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
		if (defaultOpen)
			treeNodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

		return ImGui::TreeNodeEx(name, treeNodeFlags);
	}

	void EndTreeNode()
	{
		ImGui::TreePop();
	}

	bool ColoredButton(const char* label, const ImVec4& backgroundColor, ImVec2 buttonSize)
	{
		ScopedColour buttonColor(ImGuiCol_Button, backgroundColor);
		return ImGui::Button(label, buttonSize);
	}

	bool ColoredButton(const char* label, const ImVec4& backgroundColor, const ImVec4& foregroundColor, ImVec2 buttonSize)
	{
		ScopedColour textColor(ImGuiCol_Text, foregroundColor);
		ScopedColour buttonColor(ImGuiCol_Button, backgroundColor);
		return ImGui::Button(label, buttonSize);
	}


	bool TableRowClickable(const char* id, float rowHeight)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		window->DC.CurrLineSize.y = rowHeight;

		ImGui::TableNextRow(0, rowHeight);
		ImGui::TableNextColumn();

		window->DC.CurrLineTextBaseOffset = 3.0f;
		const ImVec2 rowAreaMin = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min;
		const ImVec2 rowAreaMax = { ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), ImGui::TableGetColumnCount() - 1).Max.x, rowAreaMin.y + rowHeight };
		const ImRect rowArea(rowAreaMin, rowAreaMax);
		if (rowArea.Min.x > rowArea.Max.x || rowArea.Min.y > rowArea.Max.y)
			return false;

		ImGui::PushClipRect(rowArea.Min, rowArea.Max, false);
		ImGui::SetNextItemAllowOverlap();

		bool isRowHovered, held;
		bool isRowClicked = ImGui::ButtonBehavior(rowArea, ImGui::GetID(id),
			&isRowHovered, &held, ImGuiButtonFlags_AllowOverlap);

		ImGui::PopClipRect();

		return isRowClicked;
	}

	void Separator(ImVec2 size, ImVec4 color)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, color);
		ImGui::BeginChild("sep", size);
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	bool IsWindowFocused(const char* windowName, const bool checkRootWindow)
	{
		ImGuiWindow* currentNavWindow = GImGui->NavWindow;

		if (checkRootWindow)
		{
			// Get the actual nav window (not e.g a table)
			ImGuiWindow* lastWindow = NULL;
			while (lastWindow != currentNavWindow)
			{
				lastWindow = currentNavWindow;
				currentNavWindow = currentNavWindow->RootWindow;
			}
		}

		return currentNavWindow == ImGui::FindWindowByName(windowName);
	}

	void HelpMarker(const char* desc)
	{
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	bool ImageButton(const Ref<Texture2D>& texture, const ImVec2& size, const ImVec4& tint)
	{
		return ImageButton(texture, size, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
	}

	void ImageToolTip(const Ref<Texture2D>& texture)
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		std::string filepath = texture->GetPath().string();
		ImGui::TextUnformatted(filepath.c_str());
		ImGui::PopTextWrapPos();
		ImGuiEx::Image(texture, ImVec2(384, 384));
		ImGui::EndTooltip();
	}

#if 0
	bool PropertyScriptReference(const char* label, UUID& outScriptID, EntityDomain entityDomain, const PropertyAssetReferenceSettings& settings, bool doPushUndo)
	{
		bool modified = false;

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		auto hold = outScriptID;

		const auto& scriptEngine = ScriptEngine::GetInstance();

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x - settings.WidthOffset;
			float itemHeight = 28.0f;

			std::string buttonText = "None";
			bool valid = true;
			if (scriptEngine.IsValidScript(entityDomain, outScriptID))
			{
				const auto& metadata = scriptEngine.GetScriptMetadata(outScriptID);
				buttonText = metadata.FullName;
			}

			if ((GImGui->CurrentItemFlags & ImGuiItemFlags_MixedValue) != 0)
				buttonText = "---";

			// PropertyAssetReference could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARSP");
			{
				ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
				ImGui::Button(GenerateLabelID(buttonText), { width, itemHeight });

				const bool isHovered = ImGui::IsItemHovered();

				if (isHovered)
				{
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						// TODO(Peter): Open script in default editor
					}
					else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						ImGui::OpenPopup(assetSearchPopupID.c_str());
					}
				}
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::ScriptSearchPopup(assetSearchPopupID.c_str(), scriptEngine, outScriptID, entityDomain, &clear))
			{
				if (clear)
					outScriptID = 0;
				modified = true;
				//s_PropertyAssetReferenceAssetHandle = outScriptID;
			}
		}

		/*if (!IsItemDisabled())
		{
			if (ImGui::BeginDragDropTarget())
			{
				auto data = ImGui::AcceptDragDropPayload("asset_payload");

				if (data)
				{
					AssetHandle assetHandle = *(AssetHandle*)data->Data;
					s_PropertyAssetReferenceAssetHandle = assetHandle;
					Ref<Asset> asset = AssetManager::GetAsset<Asset>(assetHandle);
					if (asset && asset->GetAssetType() == T::GetStaticType())
					{
						outHandle = assetHandle;
						modified = true;
					}
				}

				ImGui::EndDragDropTarget();
			}
		}*/

		ImGui::PopItemWidth();
		if (settings.AdvanceToNextColumn)
		{
			ImGui::NextColumn();
			Draw::Underline();
		}

		if (modified && doPushUndo)
			EditorStack::Get().PushCopy<decltype(hold)>(&outScriptID, hold);

		return modified;
	}
#endif
#if 0
	bool DrawFieldValue(Ref<Scene> sceneContext, std::string_view fieldName, FieldStorage& storage)
	{
		ImGui::PushID(fieldName.data());

		bool result = false;

		auto hold = storage.Copy();

		switch (storage.GetType())
		{
		case DataType::Bool:
		{
			bool value = storage.GetValue<bool>();
			if (Property(fieldName.data(), value, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::SByte:
		{
			int8_t value = storage.GetValue<int8_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Short:
		{
			int16_t value = storage.GetValue<int16_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Int:
		{
			int32_t value = storage.GetValue<int32_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Long:
		{
			int64_t value = storage.GetValue<int64_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Byte:
		{
			uint8_t value = storage.GetValue<uint8_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::UShort:
		{
			uint16_t value = storage.GetValue<uint16_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::UInt:
		{
			uint32_t value = storage.GetValue<uint32_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::ULong:
		{
			uint64_t value = storage.GetValue<uint64_t>();
			if (Property(fieldName.data(), value, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Float:
		{
			float value = storage.GetValue<float>();
			if (Property(fieldName.data(), value, 0, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Double:
		{
			double value = storage.GetValue<double>();
			if (Property(fieldName.data(), value, 0, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::String:
		{
			std::string value = storage.GetValue<std::string>();

			// TODO(Emily): `Property` looks like it expects a C string but we're passing string_view data which may
			//				Not be null-terminated or may not be intended to read to terminator.
			if (Property(fieldName.data(), value, "", false))
			{
				storage.SetValue<std::string>(value);
				result = true;
			}
			break;
		}
		case DataType::Vector2:
		{
			glm::vec2 value = storage.GetValue<glm::vec2>();
			if (Property(fieldName.data(), value, 0, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Vector3:
		{
			glm::vec3 value = storage.GetValue<glm::vec3>();
			if (Property(fieldName.data(), value, 0, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Vector4:
		{
			glm::vec4 value = storage.GetValue<glm::vec4>();
			if (Property(fieldName.data(), value, 0, 0, 0, "", false))
			{
				storage.SetValue(value);
				result = true;
			}
			break;
		}
		case DataType::Prefab:
		{
			/*if (sceneContext->IsPlaying())
			{
				Coral::ManagedObject prefabObj = storage.GetValue<Coral::ManagedObject>();
				AssetHandle handle = prefabObj.GetFieldValue<uint64_t>("Handle");
				if (PropertyAssetReference<Prefab>(fieldName.data(), handle))
				{
					prefabObj.SetFieldValue("Handle", (uint64_t)handle);
					result = true;
				}
			}
			else*/
			if (!sceneContext->IsPlaying())
			{
				AssetHandle handle = storage.GetValue<uint64_t>();
				if (PropertyAssetReference<Prefab>(fieldName.data(), handle, "", 0, {}, false))
				{
					storage.SetValue(handle);
					result = true;
				}
			}

			break;
		}
		case DataType::Entity:
		{
			/*if (sceneContext->IsPlaying())
			{
				Coral::ManagedObject entityObj = storage.GetValue<Coral::ManagedObject>();
				UUID uuid = entityObj.GetFieldValue<uint64_t>("ID");
				if (PropertyEntityReference(fieldName.data(), uuid, sceneContext))
				{
					entityObj.SetFieldValue("ID", (uint64_t)uuid);
					result = true;
				}
			}
			else*/
			if (!sceneContext->IsPlaying())
			{
				UUID uuid = storage.GetValue<UUID>();
				if (PropertyEntityReference(fieldName.data(), uuid, sceneContext, "", false))
				{
					storage.SetValue(uuid);
					result = true;
				}
			}

			break;
		}
		case DataType::Mesh:
		{
			if (!sceneContext->IsPlaying())
			{
				AssetHandle handle = storage.GetValue<AssetHandle>();
				if (PropertyAssetReference<Mesh>(fieldName.data(), handle, "", 0, {}, false))
				{
					storage.SetValue(handle);
					result = true;
				}
			}
			break;
		}
		case DataType::StaticMesh:
		{
			if (!sceneContext->IsPlaying())
			{
				AssetHandle handle = storage.GetValue<AssetHandle>();
				if (PropertyAssetReference<StaticMesh>(fieldName.data(), handle, "", 0, {}, false))
				{
					storage.SetValue(handle);
					result = true;
				}
			}
			break;
		}
		case DataType::Material:
		{
			if (!sceneContext->IsPlaying())
			{
				AssetHandle handle = storage.GetValue<AssetHandle>();
				if (PropertyAssetReference<MaterialAsset>(fieldName.data(), handle, "", 0, {}, false))
				{
					storage.SetValue(handle);
					result = true;
				}
			}
			break;
		}
		case DataType::Texture2D:
		{
			AssetHandle handle = storage.GetValue<AssetHandle>();
			if (PropertyAssetReference<Texture2D>(fieldName.data(), handle, "", 0, {}, false))
				storage.SetValue(handle);
			break;
		}
		case DataType::Scene:
		{
			if (!sceneContext->IsPlaying())
			{
				AssetHandle handle = storage.GetValue<AssetHandle>();
				if (PropertyAssetReference<Scene>(fieldName.data(), handle, "", 0, {}, false))
					storage.SetValue(handle);
			}
			break;
		}
		}

		ImGui::PopID();

		if (result)
			EditorStack::Get().PushCopy<FieldStorage>(&storage, hold);

		return result;
	}

	/* TODO: This needs a more complex undo. */
	template<typename T>
	static bool PropertyAssetReferenceArray(const char* label, AssetHandle& outHandle, FieldStorage& arrayStorage, uint32_t index, intptr_t& elementToRemove, const char* helpText = "", const PropertyAssetReferenceSettings& settings = {})
	{
		bool modified = false;

		ImGuiStyle& style = ImGui::GetStyle();

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);

		if (std::strlen(helpText) != 0)
		{
			ImGui::SameLine();
			HelpMarker(helpText);
		}

		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		const float buttonSize = ImGui::GetFrameHeight();
		float itemWidth = ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x));
		ImGui::SetNextItemWidth(itemWidth);

		ImVec2 originalButtonTextAlign = style.ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float itemHeight = 28.0f;

			std::string buttonText = "Null";
			bool valid = AssetManager::IsAssetValid(outHandle) && !AssetManager::IsAssetMissing(outHandle);

			if (valid)
			{
				buttonText = Project::GetEditorAssetManager()->GetMetadata(outHandle).FilePath.stem().string();
			}

			// PropertyAssetReferenceTarget could be called multiple times in same "context"
			// and so we need a unique id for the asset search popup each time.
			// notes
			// - don't use GenerateID(), that's inviting id clashes, which would be super confusing.
			// - don't store return from GenerateLabelId in a const char* here. Because its pointing to an internal
			//   buffer which may get overwritten by the time you want to use it later on.
			std::string assetSearchPopupID = GenerateLabelID("ARTSP");
			{
				ImGuiEx::ScopedColour buttonLabelColor(ImGuiCol_Text, valid ? settings.ButtonLabelColor : settings.ButtonLabelColorError);
				ImGui::Button(GenerateLabelID(buttonText), { itemWidth, itemHeight });

				const bool isHovered = ImGui::IsItemHovered();

				if (isHovered)
				{
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						// NOTE(Peter): Ugly workaround since AssetEditorPanel includes ImGui.h (meaning ImGui.h can't include AssetEditorPanel).
						//				Will rework those includes at a later date...
						AssetEditorPanelInterface::OpenEditor(AssetManager::GetAsset<Asset>(outHandle));
					}
					else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						ImGui::OpenPopup(assetSearchPopupID.c_str());
					}
				}
			}

			ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

			bool clear = false;
			if (Widgets::AssetSearchPopup(assetSearchPopupID.c_str(), T::GetStaticType(), outHandle, &clear))
			{
				if (clear)
					outHandle = 0;
				modified = true;
			}
		}

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("asset_payload");

			if (data)
			{
				AssetHandle assetHandle = *(AssetHandle*)data->Data;
				s_PropertyAssetReferenceAssetHandle = assetHandle;
				const auto& metadata = Project::GetEditorAssetManager()->GetMetadata(assetHandle);

				if (metadata.Type == T::GetStaticType())
				{
					outHandle = assetHandle;
					modified = true;
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (modified)
			arrayStorage.SetValue<uint64_t>(outHandle, static_cast<uint64_t>(index));

		const ImVec2 backupFramePadding = style.FramePadding;
		style.FramePadding.x = style.FramePadding.y;
		ImGui::SameLine(0, style.ItemInnerSpacing.x);

		if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
			elementToRemove = intptr_t(index);

		style.FramePadding = backupFramePadding;

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		return modified;
	}

	static bool PropertyEntityReferenceArray(const char* label, UUID& entityID, Ref<Scene> context, FieldStorage& arrayStorage, uintptr_t index, intptr_t& elementToRemove)
	{
		bool receivedValidEntity = false;

		ImGuiStyle& style = ImGui::GetStyle();

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(label);
		ImGui::NextColumn();
		ShiftCursorY(4.0f);
		ImGui::PushItemWidth(-1);

		const float buttonSize = ImGui::GetFrameHeight();
		float itemWidth = ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x));
		ImGui::SetNextItemWidth(itemWidth);

		ImVec2 originalButtonTextAlign = ImGui::GetStyle().ButtonTextAlign;
		{
			ImGui::GetStyle().ButtonTextAlign = { 0.0f, 0.5f };
			float width = ImGui::GetContentRegionAvail().x;
			float itemHeight = 28.0f;

			std::string buttonText = "Null";

			Entity entity = context->TryGetEntityWithUUID(entityID);
			if (entity)
				buttonText = entity.GetComponent<TagComponent>().Tag;

			ImGui::Button(GenerateLabelID(buttonText), { width - buttonSize, itemHeight });
		}
		ImGui::GetStyle().ButtonTextAlign = originalButtonTextAlign;

		if (ImGui::BeginDragDropTarget())
		{
			auto data = ImGui::AcceptDragDropPayload("scene_entity_hierarchy");
			if (data)
			{
				entityID = *(UUID*)data->Data;
				receivedValidEntity = true;
			}

			ImGui::EndDragDropTarget();
		}

		if (receivedValidEntity)
			arrayStorage.SetValue<uint64_t>(entityID, (uint64_t)index);

		const ImVec2 backupFramePadding = style.FramePadding;
		style.FramePadding.x = style.FramePadding.y;
		ImGui::SameLine(0, style.ItemInnerSpacing.x);

		if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
			elementToRemove = index;

		style.FramePadding = backupFramePadding;

		ImGui::PopItemWidth();
		ImGui::NextColumn();

		return receivedValidEntity;
	}

	bool DrawFieldArray(Ref<Scene> sceneContext, std::string_view fieldName, FieldStorage& storage)
	{
		bool modified = false;
		intptr_t elementToRemove = -1;

		std::string arrayID = std::format("{0}FieldArray", fieldName);
		ImGui::PushID(arrayID.c_str());

		ShiftCursor(10.0f, 9.0f);
		ImGui::Text(fieldName.data());
		ImGui::NextColumn();
		ImGui::NextColumn();

		int32_t length = storage.GetLength();

		if (ImGuiEx::PropertyInput("Length", length, 1, 1, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			storage.Resize(length);
			modified = true;

			length = storage.GetLength();
		}

		ShiftCursorY(5.0f);

		auto drawScalarElement = [&storage, &elementToRemove, &arrayID](std::string_view indexString, uintptr_t index, ImGuiDataType dataType, auto& data, int32_t components, const char* format)
			{
				ImGuiStyle& style = ImGui::GetStyle();

				ShiftCursor(10.0f, 9.0f);
				ImGui::Text(indexString.data());
				ImGui::NextColumn();
				ShiftCursorY(4.0f);
				ImGui::PushItemWidth(-1);

				const float buttonSize = ImGui::GetFrameHeight();
				ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x)));

				size_t dataSize = ImGui::DataTypeGetInfo(dataType)->Size;
				if (components > 1)
				{
					if (ImGuiEx::DragScalarN(GenerateID(), dataType, &data, components, 1.0f, (const void*)0, (const void*)0, format, 0))
						storage.SetValue<std::remove_reference_t<decltype(data)>>(data, index);
				}
				else
				{
					if (ImGuiEx::DragScalar(GenerateID(), dataType, &data, 1.0f, (const void*)0, (const void*)0, format, (ImGuiSliderFlags)0))
						storage.SetValue<std::remove_reference_t<decltype(data)>>(data, index);
				}

				const ImVec2 backupFramePadding = style.FramePadding;
				style.FramePadding.x = style.FramePadding.y;
				ImGui::SameLine(0, style.ItemInnerSpacing.x);

				if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
					elementToRemove = index;

				style.FramePadding = backupFramePadding;

				ImGui::PopItemWidth();
				ImGui::NextColumn();
				Draw::Underline();
			};

		auto drawStringElement = [&storage, &elementToRemove](std::string_view indexString, uintptr_t index, std::string& data)
			{
				ImGuiStyle& style = ImGui::GetStyle();

				ShiftCursor(10.0f, 9.0f);
				ImGui::Text(indexString.data());
				ImGui::NextColumn();
				ShiftCursorY(4.0f);
				ImGui::PushItemWidth(-1);

				const float buttonSize = ImGui::GetFrameHeight();
				ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::CalcItemWidth() - (buttonSize + style.ItemInnerSpacing.x)));

				if (ImGuiEx::InputText(GenerateID(), &data))
					storage.SetValue<std::string>(data, index);

				const ImVec2 backupFramePadding = style.FramePadding;
				style.FramePadding.x = style.FramePadding.y;
				ImGui::SameLine(0, style.ItemInnerSpacing.x);

				if (ImGui::Button(GenerateLabelID("x"), ImVec2(buttonSize, buttonSize)))
					elementToRemove = index;

				style.FramePadding = backupFramePadding;

				ImGui::PopItemWidth();
				ImGui::NextColumn();
				Draw::Underline();
			};

		for (int32_t i = 0; i < length; i++)
		{
			std::string idString = std::format("[{0}]{1}-{0}", i, arrayID);
			std::string indexString = std::format("[{0}]", i);

			ImGui::PushID(idString.c_str());

			switch (storage.GetType())
			{
			case DataType::Bool:
			{
				bool value = storage.GetValue<bool>(i);
				if (ImGuiEx::Property(indexString.c_str(), value))
				{
					storage.SetValue(value, i);
					modified = true;
				}
				break;
			}
			case DataType::SByte:
			{
				int8_t value = storage.GetValue<int8_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S8, value, 1, "%d");
				break;
			}
			case DataType::Short:
			{
				int16_t value = storage.GetValue<int16_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S16, value, 1, "%d");
				break;
			}
			case DataType::Int:
			{
				int32_t value = storage.GetValue<int32_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S32, value, 1, "%d");
				break;
			}
			case DataType::Long:
			{
				int64_t value = storage.GetValue<int64_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_S64, value, 1, "%d");
				break;
			}
			case DataType::Byte:
			{
				uint8_t value = storage.GetValue<uint8_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U8, value, 1, "%d");
				break;
			}
			case DataType::UShort:
			{
				uint16_t value = storage.GetValue<uint16_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U16, value, 1, "%d");
				break;
			}
			case DataType::UInt:
			{
				uint32_t value = storage.GetValue<uint32_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U32, value, 1, "%d");
				break;
			}
			case DataType::ULong:
			{
				uint64_t value = storage.GetValue<uint64_t>(i);
				drawScalarElement(indexString, i, ImGuiDataType_U64, value, 1, "%d");
				break;
			}
			case DataType::Float:
			{
				float value = storage.GetValue<float>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 1, "%.3f");
				break;
			}
			case DataType::Double:
			{
				double value = storage.GetValue<double>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Double, value, 1, "%.6f");
				break;
			}
			case DataType::String:
			{
				std::string value = storage.GetValue<std::string>(i);
				drawStringElement(indexString, i, value);
				break;
			}
			case DataType::Vector2:
			{
				glm::vec2 value = storage.GetValue<glm::vec2>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 2, "%.3f");
				break;
			}
			case DataType::Vector3:
			{
				glm::vec3 value = storage.GetValue<glm::vec3>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 3, "%.3f");
				break;
			}
			case DataType::Vector4:
			{
				glm::vec4 value = storage.GetValue<glm::vec4>(i);
				drawScalarElement(indexString, i, ImGuiDataType_Float, value, 4, "%.3f");
				break;
			}
			case DataType::Prefab:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Prefab>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			case DataType::Entity:
			{
				UUID uuid = storage.GetValue<UUID>(i);
				PropertyEntityReferenceArray(indexString.c_str(), uuid, sceneContext, storage, i, elementToRemove);
				break;
			}
			case DataType::Mesh:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Mesh>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			case DataType::StaticMesh:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<StaticMesh>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			case DataType::Material:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<MaterialAsset>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			/*case UnmanagedType::PhysicsMaterial:
			{
				ColliderMaterial material = storage->GetValue<ColliderMaterial>(managedInstance);

				if (PropertyGridHeader(fmt::format("{} (ColliderMaterial)", fieldName)))
				{
					if (Property("Friction", material.Friction))
					{
						storage->SetValue(managedInstance, material);
						result = true;
					}

					if (Property("Restitution", material.Restitution))
					{
						storage->SetValue(managedInstance, material);
						result = true;
					}

					EndTreeNode();
				}

				AssetHandle handle = valueWrapper.GetOrDefault<AssetHandle>(0);
				PropertyAssetReferenceArray<PhysicsMaterial>(indexString.c_str(), handle, managedInstance, arrayStorage, i, elementToRemove);
				break;
			}*/
			case DataType::Texture2D:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Texture2D>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			case DataType::Scene:
			{
				AssetHandle handle = storage.GetValue<AssetHandle>(i);
				PropertyAssetReferenceArray<Scene>(indexString.c_str(), handle, storage, i, elementToRemove);
				break;
			}
			}

			ImGui::PopID();
		}

		ImGui::PopID();

		if (elementToRemove != -1)
		{
			storage.RemoveAt(elementToRemove);
			modified = true;
		}

		return modified;
	}
#endif
}
