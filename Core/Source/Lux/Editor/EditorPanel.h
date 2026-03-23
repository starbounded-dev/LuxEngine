#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Project/Project.h"
#include "Lux/Core/Events/Event.h"

namespace Lux {

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
