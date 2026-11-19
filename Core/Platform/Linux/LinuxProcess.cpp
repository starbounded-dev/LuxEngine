// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Utilities/Process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <thread>

namespace Lux
{
	ProcessResult Process::Run(const ProcessOptions& options)
	{
		ProcessResult result;
		if (options.Arguments.empty())
		{
			result.Error = "no program to run";
			return result;
		}

		// Everything the child needs is made before the fork: only async-signal-safe calls run after it.
		std::vector<char*> argv;
		for (const std::string& argument : options.Arguments)
			argv.push_back(const_cast<char*>(argument.c_str()));
		argv.push_back(nullptr);
		const std::string directory = options.WorkingDirectory.string();

		int pipeEnds[2];
		if (pipe(pipeEnds) != 0)
		{
			result.Error = "could not create a pipe";
			return result;
		}

		const pid_t child = fork();
		if (child < 0)
		{
			close(pipeEnds[0]);
			close(pipeEnds[1]);
			result.Error = "could not fork";
			return result;
		}
		if (child == 0)
		{
			dup2(pipeEnds[1], STDOUT_FILENO);
			dup2(pipeEnds[1], STDERR_FILENO);
			const int null = open("/dev/null", O_RDONLY);
			if (null >= 0)
				dup2(null, STDIN_FILENO);
			close(pipeEnds[0]);
			close(pipeEnds[1]);
			if (!directory.empty() && chdir(directory.c_str()) != 0)
				_exit(126);
			execvp(argv[0], argv.data());
			_exit(127); // not found or not executable
		}

		close(pipeEnds[1]);
		result.Started = true;

		std::string pending;
		const auto flush = [&](bool all)
		{
			size_t begin = 0;
			for (size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', begin))
			{
				std::string_view line(pending.data() + begin, end - begin);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);
				if (options.OnLine)
					options.OnLine(line);
				begin = end + 1;
			}
			pending.erase(0, begin);
			if (all && !pending.empty())
			{
				if (options.OnLine)
					options.OnLine(pending);
				pending.clear();
			}
		};

		char buffer[4096];
		for (;;)
		{
			if (options.Cancel && options.Cancel->load(std::memory_order_relaxed))
			{
				kill(child, SIGTERM);
				result.Cancelled = true;
				break;
			}

			pollfd descriptor{ pipeEnds[0], POLLIN, 0 };
			const int ready = poll(&descriptor, 1, 50);
			if (ready < 0 && errno == EINTR)
				continue;
			if (ready > 0)
			{
				const ssize_t read = ::read(pipeEnds[0], buffer, sizeof(buffer));
				if (read > 0)
				{
					pending.append(buffer, static_cast<size_t>(read));
					flush(false);
					continue;
				}
				if (read == 0)
					break; // every writer closed: the program ended
				if (errno != EINTR && errno != EAGAIN)
					break;
			}
		}
		flush(true);
		close(pipeEnds[0]);

		int status = 0;
		if (result.Cancelled)
		{
			// Give it a moment to leave, then insist.
			for (int i = 0; i < 100 && waitpid(child, &status, WNOHANG) == 0; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			kill(child, SIGKILL);
		}
		waitpid(child, &status, 0);
		result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
		if (!result.Cancelled && result.ExitCode == 127)
			result.Error = options.Arguments.front() + " was not found or could not be run";
		return result;
	}
}
