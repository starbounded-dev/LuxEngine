// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorConsolePanel.h"

#include <spdlog/sinks/base_sink.h>

#include <format>
#include <mutex>

namespace Lux {

	class EditorConsoleSink : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit EditorConsoleSink(uint32_t bufferCapacity)
			: m_MessageBufferCapacity(bufferCapacity), m_MessageBuffer(bufferCapacity) {
		}

		virtual ~EditorConsoleSink() = default;

		EditorConsoleSink(const EditorConsoleSink& other) = delete;
		EditorConsoleSink& operator=(const EditorConsoleSink& other) = delete;

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			spdlog::memory_buf_t formatted;
			spdlog::sinks::base_sink<std::mutex>::formatter_->format(msg, formatted);
			std::string longMessage = formatted;
			// The formatter appends an end-of-line; strip it so it isn't mistaken for a multi-line message.
			while (!longMessage.empty() && (longMessage.back() == '\n' || longMessage.back() == '\r'))
				longMessage.pop_back();
			std::string shortMessage = longMessage;

			// Rows are one line tall; multi-line messages (shader compiler output, validation
			// errors) show their first line here and the rest in the detail popup.
			size_t newlinePos = shortMessage.find_first_of("\r\n");
			if (newlinePos != std::string::npos)
				shortMessage = shortMessage.substr(0, newlinePos) + " ...";

			if (shortMessage.length() > 100)
			{
				size_t spacePos = shortMessage.find_first_of(' ', 100);
				if (spacePos != std::string::npos)
					shortMessage.replace(spacePos, shortMessage.length() - 1, "...");
			}

			m_MessageBuffer[m_MessageCount++] = ConsoleMessage{ shortMessage, longMessage, GetMessageTag(msg), GetMessageFlags(msg.level), std::chrono::system_clock::to_time_t(msg.time) };

			if (m_MessageCount == m_MessageBufferCapacity)
				flush_();
		}

		void flush_() override
		{
			for (uint32_t i = 0; i < m_MessageCount; i++)
			{
				const auto& message = m_MessageBuffer[i];
				if (message.Flags == (int16_t)ConsoleMessageFlags::None)
					continue;
				EditorConsolePanel::PushMessage(message);
			}

			m_MessageCount = 0;
		}

	private:
		// Tagged log macros print "[Tag] message" (Log::PrintMessageTag); anything else is grouped
		// under the logger that printed it.
		static std::string GetMessageTag(const spdlog::details::log_msg& msg)
		{
			static constexpr size_t MaxTagLength = 32;

			const std::string_view payload(msg.payload.data(), msg.payload.size());
			if (payload.size() > 2 && payload[0] == '[')
			{
				const size_t closePos = payload.find(']');
				if (closePos != std::string_view::npos && closePos > 1 && closePos <= MaxTagLength)
					return std::string(payload.substr(1, closePos - 1));
			}

			return std::string(msg.logger_name.data(), msg.logger_name.size());
		}

		static int16_t GetMessageFlags(spdlog::level::level_enum level)
		{
			int16_t flags = 0;

			switch (level)
			{
			case spdlog::level::trace:
			case spdlog::level::debug:
			case spdlog::level::info:
			{
				flags |= (int16_t)ConsoleMessageFlags::Info;
				break;
			}
			case spdlog::level::warn:
			{
				flags |= (int16_t)ConsoleMessageFlags::Warning;
				break;
			}
			case spdlog::level::err:
			case spdlog::level::critical:
			{
				flags |= (int16_t)ConsoleMessageFlags::Error;
				break;
			}
			}

			return flags;
		}

	private:
		uint32_t m_MessageBufferCapacity;
		std::vector<ConsoleMessage> m_MessageBuffer;
		uint32_t m_MessageCount = 0;
	};

}
