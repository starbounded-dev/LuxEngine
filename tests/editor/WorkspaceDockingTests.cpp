// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "Workspace/WorkspaceDocking.h"
#include "Lux/ImGui/ImGuiScaling.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", message);
			std::exit(1);
		}
	}

	void Frame(Lux::WorkspaceDocking* layout, ImVec2 size, bool unrelatedActive = false)
	{
		ImGui::GetIO().DisplaySize = size;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0});
		ImGui::SetNextWindowSize(size);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
		const ImVec2 content = ImGui::GetContentRegionAvail();
		if (unrelatedActive)
			ImGui::SetActiveID(ImGui::GetID("UnrelatedButton"), ImGui::GetCurrentWindow());
		if (layout)
			layout->Prepare(42, content);
		if (unrelatedActive)
			ImGui::ClearActiveID();
		ImGui::DockSpace(42, content);
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::Begin("Left");
		ImGui::TextUnformatted("Left");
		ImGui::End();
		ImGui::Begin("Center");
		ImGui::End();
		ImGui::Render();
	}

	void Build()
	{
		ImGui::NewFrame();
		ImGui::Begin("Host");
		ImGui::DockBuilderRemoveNode(42);
		ImGui::DockBuilderAddNode(42, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(42, {3840, 2160});
		ImGuiID center = 42;
		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.25f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Left", left);
		ImGui::DockBuilderDockWindow("Center", center);
		ImGui::DockBuilderFinish(42);
		ImGui::End();
		ImGui::Render();
	}
} // namespace

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	ImGui::CreateContext();
	Lux::ImGuiEx::DpiStyle dpi;
	dpi.Capture(ImGui::GetStyle());
	const float originalPadding = ImGui::GetStyle().WindowPadding.x;
	for (int cycle = 0; cycle < 20; ++cycle)
	{
		dpi.Apply(ImGui::GetStyle(), 1.5f);
		Check(ImGui::GetStyle().WindowPadding.x == std::floor(originalPadding * 1.5f), "DPI scales from baseline");
		dpi.Apply(ImGui::GetStyle(), 1.0f);
		Check(ImGui::GetStyle().WindowPadding.x == originalPadding, "DPI round trips do not accumulate rounding");
	}
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.Fonts->AddFontDefault();
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	Frame(nullptr, {3840, 2160});
	Build();
	Frame(nullptr, {3840, 2160});
	Frame(nullptr, {3840, 2160});
	Frame(nullptr, {1920, 1080});
	const auto* root = ImGui::DockBuilderGetNode(42);
	Check(root && root->IsSplitNode(), "test fixture has a split dockspace");
	const float fixedRatio = root->ChildNodes[0]->Size.x / root->Size.x;
	Check(fixedRatio > 0.45f, "baseline reproduces fixed-pixel sidebar consuming half the 1080p host");
	std::printf("Baseline: 25%% sidebar becomes %.1f%% on 1080p\n", fixedRatio * 100.0f);

	Build();
	Lux::WorkspaceDocking layout;
	Frame(&layout, {3840, 2160});
	Frame(&layout, {3840, 2160});
	for (int cycle = 0; cycle < 20; ++cycle)
	{
		Frame(&layout, {1920, 1080});
		root = ImGui::DockBuilderGetNode(42);
		Check(std::abs(root->ChildNodes[0]->Size.x / root->Size.x - 0.25f) < 0.005f, "1080p preserves split intent");
		Frame(&layout, {100, 150});
		Frame(&layout, {100, 150}, true);
		Frame(&layout, {3840, 2160});
		root = ImGui::DockBuilderGetNode(42);
		Check(std::abs(root->ChildNodes[0]->Size.x / root->Size.x - 0.25f) < 0.005f,
			  "small-host constraints do not destroy original proportions");
	}
	Frame(&layout, {1920, 1080});
	root = ImGui::DockBuilderGetNode(42);
	const float splitterX = root->ChildNodes[0]->Pos.x + root->ChildNodes[0]->Size.x + 1.0f;
	const float splitterY = root->Pos.y + root->Size.y * 0.5f;
	io.AddMousePosEvent(splitterX, splitterY);
	Frame(&layout, {1920, 1080});
	io.AddMouseButtonEvent(0, true);
	Frame(&layout, {1920, 1080});
	io.AddMousePosEvent(splitterX + 200.0f, splitterY);
	Frame(&layout, {1920, 1080});
	io.AddMouseButtonEvent(0, false);
	Frame(&layout, {1920, 1080});
	root = ImGui::DockBuilderGetNode(42);
	const float editedRatio = root->ChildNodes[0]->Size.x / root->Size.x;
	Check(editedRatio > 0.33f && editedRatio < 0.38f, "actual splitter drag updates the arrangement");
	Frame(&layout, {3840, 2160});
	root = ImGui::DockBuilderGetNode(42);
	Check(std::abs(root->ChildNodes[0]->Size.x / root->Size.x - editedRatio) < 0.005f,
		  "user splitter edits remain adaptive after release");
	ImGui::NewFrame();
	ImGui::Begin("Host");
	ImGui::DockBuilderRemoveNode(42);
	ImGui::DockBuilderAddNode(42, ImGuiDockNodeFlags_DockSpace);
	ImGui::End();
	ImGui::Render();
	layout.Reset();
	Frame(&layout, {1920, 1080});
	Check(!ImGui::DockBuilderGetNode(42)->IsSplitNode(), "an unsplit layout is valid and not rebuilt");
	ImGui::DestroyContext();
	std::puts("PASS: adaptive docking, 20 size round trips, constrained hosts, valid unsplit layout");
}
