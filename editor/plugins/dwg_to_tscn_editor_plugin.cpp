#include "dwg_to_tscn_editor_plugin.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/string/translation.h"
#include "core/config/project_settings.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "scene/2d/line_2d.h"
#include "scene/2d/node_2d.h"
#include "scene/gui/dialogs.h"
#include "scene/resources/packed_scene.h"

#ifdef LIBREDWG_ENABLED
#include "dwg.h"
#endif

class DwgToTscnContextMenuPlugin : public EditorContextMenuPlugin {
	GDCLASS(DwgToTscnContextMenuPlugin, EditorContextMenuPlugin);

	DwgToTscnEditorPlugin *owner = nullptr;

protected:
	static void _bind_methods() {}

public:
	void set_owner(DwgToTscnEditorPlugin *p_owner) { owner = p_owner; }

	virtual void get_options(const Vector<String> &p_paths) override {
		if (p_paths.size() != 1) {
			return;
		}
		const String &path = p_paths[0];
		if (path.get_extension().to_lower() != "dwg") {
			return;
		}
		if (owner == nullptr) {
			return;
		}
		add_context_menu_item(TTR("Convert to TSCN"), callable_mp(owner, &DwgToTscnEditorPlugin::_dwg_to_tscn_from_paths), Ref<Texture2D>());
	}
};

DwgToTscnEditorPlugin::DwgToTscnEditorPlugin() {
	context_menu_plugin.instantiate();
	context_menu_plugin->set_owner(this);
	add_context_menu_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, context_menu_plugin);

	overwrite_confirm = memnew(ConfirmationDialog);
	overwrite_confirm->set_ok_button_text(TTR("Overwrite"));
	overwrite_confirm->connect(SceneStringName(confirmed), callable_mp(this, &DwgToTscnEditorPlugin::_confirm_overwrite));
	add_child(overwrite_confirm);
}

DwgToTscnEditorPlugin::~DwgToTscnEditorPlugin() {
	if (context_menu_plugin.is_valid()) {
		remove_context_menu_plugin(context_menu_plugin);
		context_menu_plugin.unref();
	}
}

void DwgToTscnEditorPlugin::_confirm_overwrite() {
	String src = pending_source_path;
	pending_source_path = "";
	if (src.is_empty()) {
		return;
	}
	_dwg_to_tscn(src, true);
}

void DwgToTscnEditorPlugin::_dwg_to_tscn_from_paths(const Vector<String> &p_paths) {
	if (p_paths.size() != 1) {
		EditorNode::get_singleton()->show_warning(TTR("Please select exactly one file."), TTR("Error!"));
		return;
	}
	_dwg_to_tscn(p_paths[0], false);
}

void DwgToTscnEditorPlugin::_dwg_to_tscn(const String &p_source_path, bool p_force_overwrite) {
	ERR_FAIL_COND_MSG(p_source_path.is_empty(), "Source path is empty.");

	if (p_source_path.get_extension().to_lower() != "dwg") {
		EditorNode::get_singleton()->show_warning(TTR("Selected file is not a .dwg."), TTR("Error!"));
		return;
	}

	const String output_path = p_source_path.get_basename() + ".tscn";
	if (!p_force_overwrite && FileAccess::exists(output_path)) {
		pending_source_path = p_source_path;
		overwrite_confirm->set_text(vformat(TTR("File already exists:\n\n%s\n\nOverwrite?"), output_path));
		overwrite_confirm->popup_centered();
		return;
	}

	Ref<PackedScene> packed;
	packed.instantiate();

	Node2D *root = memnew(Node2D);
	root->set_name("DWG");

#ifdef LIBREDWG_ENABLED
	Dwg_Data dwg;
	memset(&dwg, 0, sizeof(Dwg_Data));
	// Minimal parse, no verbose logging.
	dwg.opts = DWG_OPTS_MINIMAL;

	const String global_path = ProjectSettings::get_singleton()->globalize_path(p_source_path);
	const int read_err = dwg_read_file(global_path.utf8().get_data(), &dwg);
	if (read_err != 0) {
		if ((read_err & DWG_ERR_CRITICAL) != 0) {
			memdelete(root);
			EditorNode::get_singleton()->show_warning(vformat(TTR("Failed to read DWG (%d):\n\n%s"), read_err, p_source_path), TTR("Error!"));
			return;
		}
		EditorNode::get_singleton()->show_warning(vformat(TTR("DWG loaded with warnings (%d):\n\n%s"), read_err, p_source_path), TTR("Warning!"));
	}

	int line_count = 0;
	for (BITCODE_BL i = 0; i < dwg.num_objects; i++) {
		Dwg_Object *obj = &dwg.object[i];
		if (obj->supertype != DWG_SUPERTYPE_ENTITY) {
			continue;
		}
		if (obj->fixedtype != DWG_TYPE_LINE) {
			continue;
		}

		Dwg_Entity_LINE *line = obj->tio.entity->tio.LINE;
		if (line == nullptr) {
			continue;
		}

		Line2D *line2d = memnew(Line2D);
		line2d->set_name(vformat("LINE_%d", line_count));
		line2d->add_point(Vector2((real_t)line->start.x, (real_t)-line->start.y));
		line2d->add_point(Vector2((real_t)line->end.x, (real_t)-line->end.y));
		line2d->set_default_color(Color(1, 1, 1));
		line2d->set_width(1.0);
		root->add_child(line2d);
		line2d->set_owner(root);
		line_count++;
	}

	dwg_free(&dwg);

	if (line_count == 0) {
		memdelete(root);
		EditorNode::get_singleton()->show_warning(TTR("DWG loaded, but no LINE entities were found to convert."), TTR("Warning!"));
		return;
	}
#else
	memdelete(root);
	EditorNode::get_singleton()->show_warning(TTR("LIBREDWG_ENABLED is not set; DWG import is not available in this build."), TTR("Error!"));
	return;
#endif

	const Error pack_err = packed->pack(root);
	memdelete(root);
	if (pack_err != OK) {
		EditorNode::get_singleton()->show_warning(TTR("Failed to pack scene."), TTR("Error!"));
		return;
	}

	const Error save_err = ResourceSaver::save(packed, output_path);
	if (save_err != OK) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Failed to save scene (%d)."), (int)save_err), TTR("Error!"));
		return;
	}
}
