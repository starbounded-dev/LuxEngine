#include "lpch.h"
#include "ConsolePanel.h"

#include <imgui/imgui.h>
#include "ImGui/ImGuiEx.h"

namespace Lux {

	ConsolePanel::ConsolePanel()
	{
		// Register the logging callback
		Log::SetConsoleCallback([this](uint8_t level, const std::string& message) {
			OnLogMessage(level, message);
		});
	}

	ConsolePanel::~ConsolePanel()
	{
		// Clear the callback to prevent dangling pointer
		Log::ClearConsoleCallback();
	}

	void ConsolePanel::OnLogMessage(uint8_t level, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(m_MessageMutex);
		
		LogMessage msg;
		msg.Level = static_cast<Log::Level>(level);
		msg.Message = message;
		
		m_Messages.push_back(std::move(msg));
		
		// Trim to max size
		while (m_Messages.size() > MaxMessages)
			m_Messages.pop_front();
	}

	void ConsolePanel::Clear()
	{
		std::lock_guard<std::mutex> lock(m_MessageMutex);
		m_Messages.clear();
	}

	void ConsolePanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Console", &isOpen);

		// Toolbar
		if (ImGui::Button("Clear"))
			Clear();

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Auto-scroll", m_AutoScroll);
		ImGuiEx::Property("Trace", m_ShowTrace);
		ImGuiEx::Property("Info", m_ShowInfo);
		ImGuiEx::Property("Warn", m_ShowWarn);
		ImGuiEx::Property("Error", m_ShowError);
		ImGuiEx::EndPropertyGrid();
		ImGui::Separator();

		// Filter input
		ImGui::Text("Filter:");
		ImGui::SameLine();
		ImGui::InputText("##filter", m_FilterBuffer, sizeof(m_FilterBuffer));

		ImGui::Separator();

		// Messages list
		ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

		{
			std::lock_guard<std::mutex> lock(m_MessageMutex);
			
			for (const auto& msg : m_Messages)
			{
				// Filter by level
				switch (msg.Level)
				{
				case Log::Level::Trace:
					if (!m_ShowTrace) continue;
					break;
				case Log::Level::Info:
					if (!m_ShowInfo) continue;
					break;
				case Log::Level::Warn:
					if (!m_ShowWarn) continue;
					break;
				case Log::Level::Error:
				case Log::Level::Fatal:
					if (!m_ShowError) continue;
					break;
				}

				// Filter by text
				if (m_FilterBuffer[0] != '\0')
				{
					if (msg.Message.find(m_FilterBuffer) == std::string::npos)
						continue;
				}

				// Set color based on level
				ImVec4 color;
				switch (msg.Level)
				{
				case Log::Level::Trace:
					color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
					break;
				case Log::Level::Info:
					color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
					break;
				case Log::Level::Warn:
					color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
					break;
				case Log::Level::Error:
					color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
					break;
				case Log::Level::Fatal:
					color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
					break;
				default:
					color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
					break;
				}

				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextUnformatted(msg.Message.c_str());
				ImGui::PopStyleColor();
			}
		}

		// Auto-scroll
		if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();

		ImGui::End();
	}

}
