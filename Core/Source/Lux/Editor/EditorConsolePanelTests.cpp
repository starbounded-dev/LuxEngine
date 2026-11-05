// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorConsolePanel.h"

#ifndef LUX_DIST
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include <imgui/imgui_internal.h>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	bool EditorConsolePanel::RunSelfTests(std::vector<std::string>& failures)
	{
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
		auto* previous = ImGui::GetCurrentContext();
		auto* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		try
		{
			auto require = [](bool value, const char* reason)
			{
				if (!value)
					throw std::runtime_error(reason);
			};
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigDebugHighlightIdConflicts = true;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int fontWidth = 0, fontHeight = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &fontWidth, &fontHeight);
			OnProjectChanged(nullptr);
			for (int row = 0; row < 180; ++row)
				AppendMessage({"Repeated ## literal message", std::format("Full message {}", row),
							   row % 2 ? "B" : "A", static_cast<int16_t>(BIT(row % 3)),
							   static_cast<time_t>(1700000000 + row)});
			AppendMessage({"Short summary", "First line\nUniqueTail % literal text", "Parser",
						   static_cast<int16_t>(ConsoleMessageFlags::Error),
						   static_cast<time_t>(1700000000)});
			auto frame = [&](float width, bool openDetails = false)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 600.0f});
				ImGui::Begin("Log");
				RenderMenu();
				RenderConsole(ImGui::GetContentRegionAvail());
				if (openDetails)
				{
					m_DetailedMessage = "Owned message\n" + std::string(3000, 'X');
					m_DetailedCaption = "Error | Parser";
					m_DetailsPopupID = ImGui::GetID("Detailed Message");
					m_DetailedPanelOpen = true;
					ImGui::OpenPopup("Detailed Message");
				}
				RenderDetails();
				ImGui::End();
				ImGui::Render();
			};
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1100.0f, 280.0f, 190.0f, 1100.0f})
				{
					frame(width);
					frame(width);
					const auto* log = ImGui::FindWindowByName("Log");
					require(log && log->ContentSize.x <= log->Size.x,
							"Log controls overflow on narrow/scaled reflow");
					require(m_VisibleMessages.size() == 181, "Log reflow dropped or duplicated messages");
				}
			}
			m_HiddenTags.insert("A");
			m_VisibleMessagesDirty = true;
			RebuildVisibleMessages();
			require(m_VisibleMessages.size() == 91, "Tag filtering lost matching messages");
			m_HiddenTags.clear();
			m_MessageFilters = static_cast<int16_t>(ConsoleMessageFlags::Warning);
			m_VisibleMessagesDirty = true;
			RebuildVisibleMessages();
			require(m_VisibleMessages.size() == 60, "Severity filtering returned the wrong messages");
			m_MessageFilters = static_cast<int16_t>(ConsoleMessageFlags::All);
			m_VisibleMessagesDirty = true;
			m_FocusSearch = true;
			frame(900.0f);
			frame(900.0f);
			io.AddInputCharactersUTF8("UniqueTail");
			frame(900.0f);
			frame(900.0f);
			require(m_SearchQuery == "UniqueTail" && m_VisibleMessages.size() == 1,
					"Log search did not find text beyond the short display line");
			m_IsFocused = true;
			io.WantTextInput = true;
			KeyPressedEvent typing(Key::F, 0);
			OnEvent(typing);
			require(typing.Handled && !m_FocusSearch, "Log text editing leaked a scene shortcut");
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_A, true);
			frame(900.0f);
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			io.AddKeyEvent(ImGuiKey_Backspace, true);
			frame(900.0f);
			io.AddKeyEvent(ImGuiKey_Backspace, false);
			frame(900.0f);
			require(m_SearchQuery.empty() && m_VisibleMessages.size() == 181,
					"Clearing Log search did not restore all rows");
			ImGui::ClearActiveID();
			io.WantTextInput = false;
			m_EnableScrollToLatest = true;
			frame(900.0f);
			frame(900.0f);
			frame(900.0f);
			auto* log = ImGui::FindWindowByName("Log");
			auto* table = GImGui->Tables.GetByKey(log->GetID("Console"));
			require(table && table->InnerWindow, "Log scrolling region is missing");
			auto* body = table->InnerWindow;
			if (body->Scroll.y < body->ScrollMax.y - 2.0f)
				throw std::runtime_error(
					std::format("Follow latest: scroll {} max {} content {}", body->Scroll.y,
								body->ScrollMax.y, body->ContentSize.y) +
					std::format(" target {} follow {}", body->ScrollTarget.y, m_EnableScrollToLatest));
			io.AddMousePosEvent(body->InnerRect.GetCenter().x, body->InnerRect.GetCenter().y);
			frame(900.0f);
			io.AddMouseWheelEvent(0.0f, 5.0f);
			frame(900.0f);
			frame(900.0f);
			if (m_EnableScrollToLatest || body->Scroll.y >= body->ScrollMax.y)
				throw std::runtime_error(std::format(
					"Inspection follow {} scroll {}/{} rect {} {} {} {} hovered {} font {} base {} main {}",
					m_EnableScrollToLatest, body->Scroll.y, body->ScrollMax.y, body->InnerRect.Min.x,
					body->InnerRect.Min.y, body->InnerRect.Max.x, body->InnerRect.Max.y,
					GImGui->HoveredWindow ? GImGui->HoveredWindow->Name : "none", body->FontRefSize,
					ImGui::GetStyle().FontSizeBase, ImGui::GetStyle().FontScaleMain));
			const float inspectedY = body->Scroll.y;
			PushMessage({"New arrival", "New arrival after manual inspection", "Test", 1,
						 static_cast<time_t>(1700000300)});
			DrainPendingMessages();
			frame(900.0f);
			frame(900.0f);
			require(std::abs(body->Scroll.y - inspectedY) < 2.0f,
					"New log output moved the inspected position");
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			frame(280.0f, true);
			frame(280.0f);
			const std::string detail = m_DetailedMessage;
			require(!ImGuiEx::ClosePopupByID(m_DetailsPopupID ^ 0x40u) && !GImGui->OpenPopupStack.empty(),
					"Closing an unrelated popup ID dismissed message details");
			m_MessageBuffer.clear();
			m_VisibleMessagesDirty = true;
			frame(280.0f);
			require(m_DetailedMessage == detail && m_DetailedPanelOpen,
					"Clearing log invalidated owned message details");
			OnClose();
			require(GImGui->OpenPopupStack.empty() && !m_IsFocused, "Hiding Log retained its modal/focus");
			for (int row = 0; row < 10010; ++row)
				PushMessage(
					{"Queue limit", std::to_string(row), "Bounded", 1, static_cast<time_t>(1700000000)});
			DrainPendingMessages();
			require(m_MessageBuffer.size() <= 10000 && !m_MessageBuffer.empty(),
					"Log retention lost its bound");
			const auto retainedID = m_MessageBuffer.back().ID;
			m_MessageBuffer.erase(m_MessageBuffer.begin());
			require(m_MessageBuffer.back().ID == retainedID, "Trimming changed retained row identities");
			OnProjectChanged(nullptr);
			require(m_MessageBuffer.empty() && m_SearchQuery.empty() && m_HiddenTags.empty() &&
						m_DetailedMessage.empty(),
					"Project switch retained Log context");
			frame(190.0f);
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		OnClose();
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
