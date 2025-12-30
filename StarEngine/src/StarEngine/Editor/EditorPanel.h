#pragma once

#include "StarEngine/Core/Ref.h"
#include "StarEngine/Scene/Scene.h"
#include "StarEngine/Project/Project.h"
#include "StarEngine/Core/Events/Event.h"

namespace StarEngine {

	class EditorPanel : public RefCounted
	{
	public:
		virtual ~EditorPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) = 0;
		virtual void OnEvent(Event& e) {}
		virtual void OnProjectChanged(const Ref<Project>& project){}
		virtual void SetSceneContext(const Ref<Scene>& context){}
		virtual void OnClose() {}
	};

}
