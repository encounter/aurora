if (NOT DEFINED RMLUI_SOURCE_DIR)
  message(FATAL_ERROR "RMLUI_SOURCE_DIR is required")
endif ()

set(_rmlui_widget "${RMLUI_SOURCE_DIR}/Source/Core/Elements/WidgetTextInput.cpp")
file(READ "${_rmlui_widget}" _contents)

string(FIND "${_contents}" "keyboard_showed = active;" _already_patched)
if (_already_patched GREATER_EQUAL 0)
  message(STATUS "RmlUi keyboard state patch already applied")
  return()
endif ()

string(FIND "${_contents}" "keyboard_showed = true;" _older_state_tracking)
if (_older_state_tracking GREATER_EQUAL 0)
  message(STATUS "RmlUi already tracks keyboard state")
  return()
endif ()

set(_old_block [=[
void WidgetTextInput::SetKeyboardActive(bool active)
{
	if (!keyboard_showed && !active)
		return;

	SystemInterface* system = GetSystemInterface();
	if (!system)
		return;

	if (active)
	{
		// Activate the keyboard and submit the cursor position and line height to enable clients to adjust the input method editor (IME).
		const Vector2f element_offset = parent->GetAbsoluteOffset() - Vector2f{parent->GetScrollLeft(), parent->GetScrollTop()};
		const Vector2f absolute_cursor_position = element_offset + cursor_position;
		system->ActivateKeyboard(absolute_cursor_position, cursor_size.y);
	}
	else
	{
		system->DeactivateKeyboard();
	}
}
]=])

set(_new_block [=[
void WidgetTextInput::SetKeyboardActive(bool active)
{
	if (!keyboard_showed && !active)
		return;

	SystemInterface* system = GetSystemInterface();
	if (!system)
		return;

	if (active)
	{
		// Activate the keyboard and submit the cursor position and line height to enable clients to adjust the input method editor (IME).
		const Vector2f element_offset = parent->GetAbsoluteOffset() - Vector2f{parent->GetScrollLeft(), parent->GetScrollTop()};
		const Vector2f absolute_cursor_position = element_offset + cursor_position;
		system->ActivateKeyboard(absolute_cursor_position, cursor_size.y);
	}
	else
	{
		system->DeactivateKeyboard();
	}

	keyboard_showed = active;
}
]=])

string(FIND "${_contents}" "${_old_block}" _old_pos)
if (_old_pos LESS 0)
  message(FATAL_ERROR "Failed to apply RmlUi keyboard state patch")
endif ()

string(REPLACE "${_old_block}" "${_new_block}" _contents "${_contents}")
file(WRITE "${_rmlui_widget}" "${_contents}")
message(STATUS "Applied RmlUi keyboard state patch")
