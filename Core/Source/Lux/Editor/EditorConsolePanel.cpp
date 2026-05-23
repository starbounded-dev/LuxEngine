#include "lpch.h"
#include "EditorConsolePanel.h"

#include "Lux/Core/Application.h"
//#include "Lux/Core/Events/SceneEvents.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"

#include <imgui/imgui_internal.h>

#include <format>

namespace Lux {

	static EditorConsolePanel* s_Instance = nullptr;
	static std::mutex s_PendingMessageMutex;
	static std::vector<ConsoleMessage> s_PendingMessages;

	static const ImVec4 s_InfoTint = ImVec4(0.0f, 0.431372549f, 1.0f, 1.0f);
	static const ImVec4 s_WarningTint = ImVec4(1.0f, 0.890196078f, 0.0588235294f, 1.0f);
	static const ImVec4 s_ErrorTint = ImVec4(1.0f, 0.309803922f, 0.309803922f, 1.0f);

	EditorConsolePanel::EditorConsolePanel()
	{
		LUX_CORE_ASSERT(s_Instance == nullptr);
		s_Instance = this;

		m_MessageBuffer.reserve(500);

		std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
		m_MessageBuffer.insert(m_MessageBuffer.end(), s_PendingMessages.begin(), s_PendingMessages.end());
		s_PendingMessages.clear();
	}

	EditorConsolePanel::~EditorConsolePanel()
	{
		s_Instance = nullptr;
	}

	void EditorConsolePanel::OnEvent(Event& event)
	{/*
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<ScenePreStartEvent>([this](ScenePreStartEvent& e)
			{
				if (m_ClearOnPlay)
				{
					std::scoped_lock<std::mutex> lock(m_MessageBufferMutex);
					m_MessageBuffer.clear();
				}
				return false;
			});*/
	}

	void EditorConsolePanel::OnImGuiRender(bool& isOpen)
	{
		if (ImGui::Begin(m_PanelName, &isOpen))
		{
			ImVec2 consoleSize = ImGui::GetContentRegionAvail();
			consoleSize.y -= 32.0f;

			RenderMenu({ consoleSize.x, 28.0f });
			RenderConsole(consoleSize);
		}
		ImGui::End();
	}

	void EditorConsolePanel::OnProjectChanged(const Ref<Project>& project)
	{
		std::scoped_lock<std::mutex> lock(m_MessageBufferMutex);
		m_MessageBuffer.clear();
	}

	void EditorConsolePanel::Focus()
	{
		ImGui::SetWindowFocus(m_PanelName);
	}

	void EditorConsolePanel::SetProgress(const std::string& label, float progress)
	{
		m_ProgressLabel = label;
		m_Progress = progress;

		if (m_Progress >= 1.0f)
			ClearProgress();
	}

	void EditorConsolePanel::ClearProgress()
	{
		m_ProgressLabel = std::string();
		m_Progress = 0.0f;
	}

	void EditorConsolePanel::RenderMenu(const ImVec2& size)
	{
		ImGuiEx::ScopedStyleStack frame(ImGuiStyleVar_FrameBorderSize, 0.0f, ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::BeginChild("Toolbar", size);

		const float ToolbarHeight = 28.0f;

		if (ImGui::Button("Clear", { 75.0f, ToolbarHeight }))
		{
			std::scoped_lock<std::mutex> lock(m_MessageBufferMutex);
			m_MessageBuffer.clear();
		}

		ImGui::SameLine();

		const auto& style = ImGui::GetStyle();
		const std::string clearOnPlayText = std::format("{} Clear on Play", m_ClearOnPlay ? LUX_ICON_CHECK : LUX_ICON_TIMES);
		ImVec4 textColor = m_ClearOnPlay ? style.Colors[ImGuiCol_Text] : style.Colors[ImGuiCol_TextDisabled];
		if (ImGuiEx::ColoredButton(clearOnPlayText.c_str(), GetToolbarButtonColor(m_ClearOnPlay), textColor, ImVec2(110.0f, ToolbarHeight)))
			m_ClearOnPlay = !m_ClearOnPlay;

		if (!m_ProgressLabel.empty())
		{
			ImGui::SameLine();
			std::string progressBarText = std::format("{} ({}%)", m_ProgressLabel, (int)(m_Progress * 100 + 0.01f));
			ImGui::ProgressBar(m_Progress, ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, ToolbarHeight), progressBarText.c_str());
		}

		{
			const ImVec2 buttonSize(ToolbarHeight, ToolbarHeight);

			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f, 0.0f);
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Info) ? s_InfoTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_INFO_CIRCLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Info), textColor, buttonSize))
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Info;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle info messages");

			ImGui::SameLine();
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Warning) ? s_WarningTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_EXCLAMATION_TRIANGLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Warning), textColor, buttonSize))
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Warning;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle warning and Vulkan validation warning messages");

			ImGui::SameLine();
			textColor = (m_MessageFilters & (int16_t)ConsoleMessageFlags::Error) ? s_ErrorTint : style.Colors[ImGuiCol_TextDisabled];
			if (ImGuiEx::ColoredButton(LUX_ICON_EXCLAMATION_CIRCLE, GetToolbarButtonColor(m_MessageFilters & (int16_t)ConsoleMessageFlags::Error), textColor, buttonSize))
				m_MessageFilters ^= (int16_t)ConsoleMessageFlags::Error;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle error, Vulkan validation error, and shader compile error messages");
		}

		ImGui::EndChild();
	}

	void EditorConsolePanel::RenderConsole(const ImVec2& size)
	{
		static const char* s_Columns[] = { "Type", "Timestamp", "Message" };

		ImGuiEx::Table("Console", s_Columns, 3, size, [&]()
			{
				std::scoped_lock<std::mutex> lock(m_MessageBufferMutex);

				float scrollY = ImGui::GetScrollY();
				if (scrollY < m_PreviousScrollY)
					m_EnableScrollToLatest = false;

				if (scrollY >= ImGui::GetScrollMaxY())
					m_EnableScrollToLatest = true;

				m_PreviousScrollY = scrollY;

				float rowHeight = 24.0f;
				for (uint32_t i = 0; i < m_MessageBuffer.size(); i++)
				{
					const auto& msg = m_MessageBuffer[i];

					if (!(m_MessageFilters & (int16_t)msg.Flags))
						continue;

					ImGui::PushID(&msg);

					const bool clicked = ImGuiEx::TableRowClickable(msg.ShortMessage.c_str(), rowHeight);

					ImGuiEx::Separator(ImVec2(4.0f, ImGui::CalcTextSize(msg.ShortMessage.c_str()).y), GetMessageColor(msg));
					ImGui::SameLine();
					ImGui::TextUnformatted(GetMessageType(msg));
					ImGui::TableNextColumn();
					ImGuiEx::ShiftCursorX(4.0f);

					std::stringstream timeString;
					tm* timeBuffer = localtime(&msg.Time);
					timeString << std::put_time(timeBuffer, "%T");
					ImGui::TextUnformatted(timeString.str().c_str());

					ImGui::TableNextColumn();
					ImGuiEx::ShiftCursorX(4.0f);
					ImGui::TextUnformatted(msg.ShortMessage.c_str());

					if (i == m_MessageBuffer.size() - 1 && m_ScrollToLatest)
					{
						ImGui::ScrollToItem();
						m_ScrollToLatest = false;
					}

					if (clicked)
					{
						ImGui::OpenPopup("Detailed Message");
						auto [width, height] = Application::Get().GetWindow().GetSize();
						auto [xPos, yPos] = Application::Get().GetWindow().GetWindowPos();
						//ImVec2 size = ImGui::GetMainViewport()->Size;
						ImGui::SetNextWindowSize({ (float)width * 0.5f, (float)height * 0.5f });
						ImGui::SetNextWindowPos({ xPos + (float)width / 2.0f, yPos + (float)height / 2.5f }, 0, { 0.5, 0.5 });
						m_DetailedPanelOpen = true;
					}

					if (m_DetailedPanelOpen)
					{
						ImGuiEx::ScopedStyle windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
						ImGuiEx::ScopedStyle framePadding(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 8.0f));

						if (ImGui::BeginPopupModal("Detailed Message", &m_DetailedPanelOpen, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
						{
							ImGui::TextWrapped("%s", msg.LongMessage.c_str());
							if (ImGui::Button("Copy Full Message", ImVec2(150.0f, 28.0f)))
							{
								ImGui::SetClipboardText(msg.LongMessage.c_str());
							}
							ImGui::EndPopup();
						}
					}

					if (ImGui::BeginPopupContextItem("ConsoleMessageContext"))
					{
						if (ImGui::MenuItem("Copy Full Message"))
							ImGui::SetClipboardText(msg.LongMessage.c_str());
						if (ImGui::MenuItem("Copy Short Message"))
							ImGui::SetClipboardText(msg.ShortMessage.c_str());
						ImGui::EndPopup();
					}

					ImGui::PopID();
				}
			});
	}

	const char* EditorConsolePanel::GetMessageType(const ConsoleMessage& message) const
	{
		if (message.Flags & (int16_t)ConsoleMessageFlags::Info) return "Info";
		if (message.Flags & (int16_t)ConsoleMessageFlags::Warning) return "Warning";
		if (message.Flags & (int16_t)ConsoleMessageFlags::Error) return "Error";
		return "Unknown";
	}

	const ImVec4& EditorConsolePanel::GetMessageColor(const ConsoleMessage& message) const
	{
		//if (message.Flags & (int16_t)ConsoleMessageFlags::Info) return s_InfoButtonOnTint;
		if (message.Flags & (int16_t)ConsoleMessageFlags::Warning) return s_WarningTint;
		if (message.Flags & (int16_t)ConsoleMessageFlags::Error) return s_ErrorTint;
		return s_InfoTint;
	}

	ImVec4 EditorConsolePanel::GetToolbarButtonColor(const bool value) const
	{
		const auto& style = ImGui::GetStyle();
		return value ? style.Colors[ImGuiCol_Header] : style.Colors[ImGuiCol_FrameBg];
	}

	void EditorConsolePanel::PushMessage(const ConsoleMessage& message)
	{
		if (s_Instance == nullptr)
		{
			std::scoped_lock<std::mutex> lock(s_PendingMessageMutex);
			s_PendingMessages.push_back(message);
			return;
		}

		{
			std::scoped_lock<std::mutex> lock(s_Instance->m_MessageBufferMutex);
			s_Instance->m_MessageBuffer.push_back(message);
		}

		if (s_Instance->m_EnableScrollToLatest)
			s_Instance->m_ScrollToLatest = true;
	}

}
