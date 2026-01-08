#pragma once

#include "editor/plugins/editor_plugin.h"

class EditorContextMenuPlugin;
class ConfirmationDialog;

class DwgToTscnContextMenuPlugin;

class DwgToTscnEditorPlugin : public EditorPlugin {
	GDCLASS(DwgToTscnEditorPlugin, EditorPlugin);

	friend class DwgToTscnContextMenuPlugin;

	Ref<DwgToTscnContextMenuPlugin> context_menu_plugin;
	ConfirmationDialog *overwrite_confirm = nullptr;
	String pending_source_path;

	void _dwg_to_tscn_from_paths(const Vector<String> &p_paths);
	void _dwg_to_tscn(const String &p_source_path, bool p_force_overwrite);
	void _confirm_overwrite();

public:
	DwgToTscnEditorPlugin();
	~DwgToTscnEditorPlugin();
};
