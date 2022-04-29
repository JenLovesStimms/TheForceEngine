#include "imuse.h"
#include "imSoundFader.h"
#include <TFE_System/system.h>
#include <TFE_Audio/midi.h>
#include <assert.h>

namespace TFE_Jedi
{
	////////////////////////////////////////////////////
	// Structures
	////////////////////////////////////////////////////
	struct ImTrigger
	{
		ImSoundId soundId;
		s32 marker;
		s32 opcode;
		s32 args[10];
	};

	struct ImDeferCmd
	{
		s32 time;
		s32 opcode;
		s32 args[10];
	};

	enum ImTriggerConst
	{
		ImMaxDeferredCmd = 8,
	};

	/////////////////////////////////////////////////////
	// Internal State
	/////////////////////////////////////////////////////
	static ImTrigger s_triggers[imChannelCount];
	static ImDeferCmd s_deferCmd[ImMaxDeferredCmd];
	static s32 s_imDeferredCmds = 0;

	void ImHandleDeferredCommand(ImDeferCmd* cmd);
	void ImTriggerExecute(ImTrigger* trigger, s32 marker);

	/////////////////////////////////////////////////////////// 
	// API
	/////////////////////////////////////////////////////////// 
	s32 ImSetTrigger(ImSoundId soundId, s32 marker, s32 opcode)
	{
		if (!soundId) { return imArgErr; }

		ImTrigger* trigger = s_triggers;
		for (s32 i = 0; i < imChannelCount; i++, trigger++)
		{
			if (!trigger->soundId)
			{
				trigger->soundId = soundId;
				trigger->marker = marker;
				trigger->opcode = opcode;

				// The code beyond this point handles variable parameters - but this isn't used in Dark Forces.
				// It blindly copies 10 arguments from the stack and stores them in trigger->args[]
				return imSuccess;
			}
		}
		TFE_System::logWrite(LOG_ERROR, "IMuse", "tr unable to alloc trigger.");
		return imAllocErr;
	}

	// Returns the number of matching triggers.
	// '-1' acts as a wild card.
	s32 ImCheckTrigger(ImSoundId soundId, s32 marker, s32 opcode)
	{
		ImTrigger* trigger = s_triggers;
		s32 count = 0;
		for (s32 i = 0; i < imChannelCount; i++, trigger++)
		{
			if (trigger->soundId)
			{
				if ((soundId == imSoundWildcard || soundId == trigger->soundId) &&
					(marker  == -1 || marker  == trigger->marker) &&
					(opcode  == -1 || opcode  == trigger->opcode))
				{
					count++;
				}
			}
		}
		return count;
	}

	s32 ImClearTrigger(ImSoundId soundId, s32 marker, s32 opcode)
	{
		ImTrigger* trigger = s_triggers;
		for (s32 i = 0; i < imChannelCount; i++, trigger++)
		{
			// Only clear set triggers and match Sound ID
			if (trigger->soundId && (soundId == imSoundWildcard || soundId == trigger->soundId))
			{
				// Match marker and opcode.
				if ((marker == -1 || marker == trigger->marker) && (opcode == -1 || opcode == trigger->opcode))
				{
					trigger->soundId = 0;
				}
			}
		}
		return imSuccess;
	}

	s32 ImClearTriggersAndCmds()
	{
		for (s32 i = 0; i < imChannelCount; i++)
		{
			s_triggers[i].soundId = 0;
		}
		for (s32 i = 0; i < ImMaxDeferredCmd; i++)
		{
			s_deferCmd[i].time = 0;
		}
		s_imDeferredCmds = 0;
		return imSuccess;
	}

	// The original function can take a variable number of arguments, but it is used exactly once in Dark Forces,
	// so I simplified it.
	s32 ImDeferCommand(s32 time, s32 opcode, s32 arg1)
	{
		ImDeferCmd* cmd = s_deferCmd;
		if (time == 0)
		{
			return imArgErr;
		}

		for (s32 i = 0; i < ImMaxDeferredCmd; i++, cmd++)
		{
			if (cmd->time == 0)
			{
				cmd->opcode = opcode;
				cmd->time = time;

				// This copies variable arguments into cmd->args[], for Dark Forces, only a single argument is used.
				cmd->args[0] = arg1;
				s_imDeferredCmds = 1;
				return imSuccess;
			}
		}

		TFE_System::logWrite(LOG_ERROR, "IMuse", "tr unable to alloc deferred cmd.");
		return imAllocErr;
	}
		
	void ImHandleDeferredCommands()
	{
		ImDeferCmd* cmd = s_deferCmd;
		if (s_imDeferredCmds)
		{
			s_imDeferredCmds = 0;
			for (s32 i = 0; i < ImMaxDeferredCmd; i++, cmd++)
			{
				if (cmd->time)
				{
					s_imDeferredCmds = 1;
					cmd->time--;
					if (cmd->time == 0)
					{
						ImHandleDeferredCommand(cmd);
					}
				}
			}
		}
	}

	void ImSetSoundTrigger(u32 soundId, s32 marker)
	{
		ImTrigger* trigger = s_triggers;
		// Look for the matching trigger.
		for (s32 i = 0; i < imChannelCount; i++, trigger++)
		{
			s32 triggerSndId = trigger->soundId;
			// Trigger is null or doesn't match.
			if (!triggerSndId || triggerSndId != soundId)
			{
				continue;
			}

			// The code never reaches this point in Dark Forces.
			if (trigger->marker)
			{
				TFE_System::logWrite(LOG_ERROR, "IMuse", "trigger->marker should always be 0 in Dark Forces.");
				assert(0);

				if ((marker >= 0x80 && trigger->marker == 0x80) || (marker <= 0x80 && trigger->marker == marker))
				{
					ImTriggerExecute(trigger, marker);
				}
			}
		}
	}

	////////////////////////////////////
	// Internal
	////////////////////////////////////
	void ImHandleDeferredCommand(ImDeferCmd* cmd)
	{
		// In Dark Forces, only stop sound is used, basically to deal stopping a sound after an iMuse transition.
		// In the original code, this calls the dispatch function and pushes all 10 arguments.
		// A non-dispatch version can be created here with a simple case statement.
		if (cmd->opcode == imStopSound)
		{
			ImStopSound(cmd->args[0]);
		}
		else
		{
			TFE_System::logWrite(LOG_ERROR, "IMuse", "Unimplemented deferred command.");
			assert(0);
		}
	}

	// ImTriggerExecute() is never called when running Dark Forces.
	void ImTriggerExecute(ImTrigger* trigger, s32 marker)
	{
		TFE_System::logWrite(LOG_ERROR, "IMuse", "ImTriggerExecute() is not called in Dark Forces.");
		assert(0);

		trigger->soundId = 0;
		if (trigger->opcode < imUndefined)
		{
			// In the original code, this passed in all 10 parameters.
			// ImProcessCommand(trigger->opcode, *arg0, *arg1, *arg2);
		}
		else
		{
			// This assumes that the opcode is actually a function pointer...
			// ImCommandFunc(trigger->opcode)(trigger->opcode, *arg0, *arg1, *arg2);
		}
	}

}  // namespace TFE_Jedi