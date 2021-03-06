/*
Copyright 2017 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PresentMonInterface.h"
#include "Config\Config.h"
#include "Utility\FileDirectory.h"
#include "Logging\MessageLog.h"
#include "Recording.h"
#include "Utility\Constants.h"
#include "Utility\ProcessHelper.h"
#include "Config\BlackList.h"

#include "..\PresentMon\PresentMon\PresentMon.hpp"

// avoid changing the PresentMon implementation by including main.cpp directly
#include "..\PresentMon\PresentMon\main.cpp"


std::mutex g_RecordingMutex;

PresentMonInterface::PresentMonInterface(HWND hwnd)
{
  g_hWnd = hwnd; // Tell PresentMon where to send its messages 
  args_ = new CommandLineArgs();
  g_fileDirectory.CreateDirectories();
  recording_.SetRecordingDirectory(g_fileDirectory.GetDirectory(FileDirectory::DIR_RECORDING));

  g_messageLog.Start(g_fileDirectory.GetDirectory(FileDirectory::DIR_LOG) + g_logFileName,
    "PresentMon", false);
  g_messageLog.LogOS();
}

PresentMonInterface::~PresentMonInterface()
{
  std::lock_guard<std::mutex> lock(g_RecordingMutex);
  StopRecording();
	if (args_)
	{
		delete args_;
		args_ = nullptr;
	}
}

int PresentMonInterface::GetPresentMonRecordingStopMessage()
{
  return WM_STOP_ETW_THREADS;
}

void SetPresentMonArgs(unsigned int hotkey, unsigned int timer, CommandLineArgs& args)
{
  args.mHotkeyVirtualKeyCode = hotkey;
  args.mHotkeySupport = true;
  args.mVerbosity = Verbosity::Verbose;

  if (timer > 0) {
    args.mTimer = timer;
  }

  // We want to keep our OCAT window open.
  args.mTerminateOnProcExit = false;
  args.mTerminateAfterTimer = false; 
}

void PresentMonInterface::ToggleRecording(bool recordAllProcesses, unsigned int hotkey, unsigned int timer)
{
  std::lock_guard<std::mutex> lock(g_RecordingMutex);
  if (recording_.IsRecording()) 
  {
    StopRecording();
  }
  else 
  {
    StartRecording(recordAllProcesses, hotkey, timer);
  }
}

std::string FormatCurrentTime() 
{
  struct tm tm;
  time_t time_now = time(NULL);
  localtime_s(&tm, &time_now);
  char buffer[4096];
  _snprintf_s(buffer, _TRUNCATE, "%4d%02d%02d-%02d%02d%02d",  // ISO 8601
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return std::string(buffer);
}

void PresentMonInterface::StartRecording(bool recordAllProcesses, unsigned int hotkey, unsigned int timer)
{
  assert(recording_.IsRecording() == false);

  SetPresentMonArgs(hotkey, timer, *args_);
  recording_.SetRecordAllProcesses(recordAllProcesses);

  std::stringstream outputFilePath;
  outputFilePath << recording_.GetDirectory() << "perf_";

  recording_.Start();
  if (recording_.GetRecordAllProcesses())
  {
    outputFilePath << "AllProcesses";
    args_->mTargetProcessName = nullptr;
  }
  else 
  {
    outputFilePath << recording_.GetProcessName();
    args_->mTargetProcessName = recording_.GetProcessName().c_str();
  }

  auto dateAndTime = FormatCurrentTime();
  outputFilePath << "_" << dateAndTime << "_RecordingResults";
  presentMonOutputFilePath_ = outputFilePath.str() + ".csv";
  args_->mOutputFileName = presentMonOutputFilePath_.c_str();

  // Keep the output file path in the current recording to attach 
  // its contents to the performance summary later on.
  outputFilePath << "-" << args_->mRecordingCount << ".csv";
  recording_.SetOutputFilePath(outputFilePath.str());
  recording_.SetDateAndTime(dateAndTime);

  g_messageLog.Log(MessageLog::LOG_INFO, "PresentMonInterface",
                  "Start recording " + recording_.GetProcessName());

  StartEtwThreads(*args_);
}

void PresentMonInterface::StopRecording()
{
  if (recording_.IsRecording())
  {
    g_messageLog.Log(MessageLog::LOG_INFO, "PresentMonInterface", "Stop recording");
    if (EtwThreadsRunning())
    {
      StopEtwThreads(args_);
    }
    recording_.Stop();
  }
}

const std::string PresentMonInterface::GetRecordedProcess()
{
  if (recording_.IsRecording()) 
  {
    return recording_.GetProcessName();
  }
  return "";
}

bool PresentMonInterface::CurrentlyRecording() 
{ 
  return EtwThreadsRunning();
}
