#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Core/Log.h"

#include <deque>
#include <mutex>
#include <string>

namespace Lux {

	class ConsolePanel : public EditorPanel
	{
	public:
		ConsolePanel();
		virtual ~ConsolePanel();

		virtual void OnImGuiRender(bool& isOpen) override;

		void Clear();

	private:
		void OnLogMessage(uint8_t level, const std::string& message);

		struct LogMessage
		{
			Log::Level Level;
			std::string Message;
		};

		static constexpr size_t MaxMessages = 1000;
		
		std::deque<LogMessage> m_Messages;
		std::mutex m_MessageMutex;
		
		bool m_AutoScroll = true;
		bool m_ShowTrace = true;
		bool m_ShowInfo = true;
		bool m_ShowWarn = true;
		bool m_ShowError = true;
		
		// Filter text
		char m_FilterBuffer[256] = "";
	};

}
