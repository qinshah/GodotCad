/**************************************************************************/
/*  cad_command_bar.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "cad_command_bar.h"

#include "core/os/keyboard.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/separator.h"

// 拼音首字母映射表（简化版，覆盖常用汉字）
static const char32_t PINYIN_TABLE[][2] = {
	// 这里只列出部分常用字的拼音首字母映射
	// 实际使用中可以扩展或使用外部库
	{ U'移', 'y' }, { U'动', 'd' }, { U'旋', 'x' }, { U'转', 'z' },
	{ U'缩', 's' }, { U'放', 'f' }, { U'选', 'x' }, { U'择', 'z' },
	{ U'变', 'b' }, { U'换', 'h' }, { U'工', 'g' }, { U'具', 'j' },
	{ U'模', 'm' }, { U'式', 's' }, { U'本', 'b' }, { U'地', 'd' },
	{ U'坐', 'z' }, { U'标', 'b' }, { U'吸', 'x' }, { U'附', 'f' },
	{ U'锁', 's' }, { U'定', 'd' }, { U'解', 'j' }, { U'组', 'z' },
	{ U'合', 'h' }, { U'取', 'q' }, { U'消', 'x' }, { U'视', 's' },
	{ U'图', 't' }, { U'网', 'w' }, { U'格', 'g' }, { U'原', 'y' },
	{ U'点', 'd' }, { U'环', 'h' }, { U'境', 'j' }, { U'光', 'g' },
	{ U'照', 'z' }, { U'相', 'x' }, { U'机', 'j' }, { U'预', 'y' },
	{ U'览', 'l' }, { U'尺', 'c' }, { U'子', 'z' }, { U'测', 'c' },
	{ U'量', 'l' }, { U'重', 'c' }, { U'置', 'z' }, { U'位', 'w' },
	{ 0, 0 } // 结束标记
};

void CadCommandBar::_bind_methods() {
	ClassDB::bind_method(D_METHOD("activate"), &CadCommandBar::activate);
	ClassDB::bind_method(D_METHOD("deactivate"), &CadCommandBar::deactivate);
	ClassDB::bind_method(D_METHOD("is_command_bar_active"), &CadCommandBar::is_command_bar_active);
	ClassDB::bind_method(D_METHOD("register_command", "id", "name", "description", "callback", "icon", "keywords", "shortcut_text"),
			&CadCommandBar::register_command, DEFVAL(Ref<Texture2D>()), DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("unregister_command", "id"), &CadCommandBar::unregister_command);
}

void CadCommandBar::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			load_settings();
			_load_history();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_save_history();
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (command_input) {
				command_input->set_right_icon(get_theme_icon(SNAME("Search"), EditorStringName(EditorIcons)));
			}
			if (toggle_button) {
				toggle_button->set_button_icon(get_theme_icon(SNAME("Terminal"), EditorStringName(EditorIcons)));
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorSettings::get_singleton()->check_changed_settings_in_group("editors/3d/command_bar")) {
				load_settings();
			}
		} break;
	}
}

CadCommandBar::CadCommandBar() {
	set_h_size_flags(SIZE_EXPAND_FILL);

	// 创建切换按钮
	toggle_button = memnew(Button);
	toggle_button->set_flat(true);
	toggle_button->set_tooltip_text(TTR("Toggle Command Bar (Ctrl+Space)"));
	toggle_button->connect(SceneStringName(pressed), callable_mp(this, &CadCommandBar::_on_toggle_pressed));
	add_child(toggle_button);

	// 创建命令输入框
	command_input = memnew(LineEdit);
	command_input->set_placeholder(TTR("Type command or search... (Ctrl+Space)"));
	command_input->set_h_size_flags(SIZE_EXPAND_FILL);
	command_input->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
	command_input->set_clear_button_enabled(true);
	command_input->connect(SceneStringName(text_changed), callable_mp(this, &CadCommandBar::_on_input_text_changed));
	command_input->connect("text_submitted", callable_mp(this, &CadCommandBar::_on_input_text_submitted));
	command_input->connect(SceneStringName(gui_input), callable_mp(this, &CadCommandBar::_on_input_gui_input));
	add_child(command_input);

	// 创建搜索结果弹出窗口
	results_popup = memnew(PopupPanel);
	results_popup->set_wrap_controls(true);
	results_popup->connect("popup_hide", callable_mp(this, &CadCommandBar::_on_popup_hide));

	VBoxContainer *popup_vbox = memnew(VBoxContainer);
	results_popup->add_child(popup_vbox);

	// 搜索结果树
	results_tree = memnew(Tree);
	results_tree->set_hide_root(true);
	results_tree->set_columns(2);
	results_tree->set_column_expand(0, true);
	results_tree->set_column_expand(1, false);
	results_tree->set_column_custom_minimum_width(1, 100 * EDSCALE);
	results_tree->set_custom_minimum_size(Size2(400 * EDSCALE, 200 * EDSCALE));
	results_tree->connect(SceneStringName(item_selected), callable_mp(this, &CadCommandBar::_on_result_selected));
	results_tree->connect("item_activated", callable_mp(this, &CadCommandBar::_on_result_activated));
	popup_vbox->add_child(results_tree);

	// 分隔线
	popup_vbox->add_child(memnew(HSeparator));

	// 预览面板
	preview_panel = memnew(PanelContainer);
	preview_panel->set_custom_minimum_size(Size2(0, 60 * EDSCALE));
	popup_vbox->add_child(preview_panel);

	HBoxContainer *preview_hbox = memnew(HBoxContainer);
	preview_panel->add_child(preview_hbox);

	preview_icon = memnew(TextureRect);
	preview_icon->set_custom_minimum_size(Size2(48 * EDSCALE, 48 * EDSCALE));
	preview_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	preview_hbox->add_child(preview_icon);

	VBoxContainer *preview_info = memnew(VBoxContainer);
	preview_info->set_h_size_flags(SIZE_EXPAND_FILL);
	preview_hbox->add_child(preview_info);

	preview_name = memnew(Label);
	preview_name->add_theme_font_size_override(SceneStringName(font_size), 14 * EDSCALE);
	preview_info->add_child(preview_name);

	preview_description = memnew(Label);
	preview_description->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
	preview_description->add_theme_color_override(SceneStringName(font_color), Color(0.7, 0.7, 0.7));
	preview_info->add_child(preview_description);

	preview_shortcut = memnew(Label);
	preview_shortcut->add_theme_color_override(SceneStringName(font_color), Color(0.5, 0.8, 1.0));
	preview_info->add_child(preview_shortcut);

	add_child(results_popup);

	// 默认隐藏输入框
	command_input->hide();
}

CadCommandBar::~CadCommandBar() {
	_save_history();
}


void CadCommandBar::activate() {
	if (is_active) {
		return;
	}

	is_active = true;
	command_input->show();
	command_input->grab_focus();
	command_input->select_all();

	// 显示所有命令（按最近使用排序）
	_update_search_results("");
	_position_popup();
	results_popup->popup();
}

void CadCommandBar::deactivate() {
	if (!is_active) {
		return;
	}

	is_active = false;
	command_input->clear();
	command_input->hide();
	results_popup->hide();

	// 恢复编辑器焦点
	if (spatial_editor) {
		spatial_editor->grab_focus();
	}
}

void CadCommandBar::_on_toggle_pressed() {
	if (is_active) {
		deactivate();
	} else {
		activate();
	}
}

void CadCommandBar::_on_input_text_changed(const String &p_text) {
	_update_search_results(p_text);
	if (!results_popup->is_visible() && !p_text.is_empty()) {
		_position_popup();
		results_popup->popup();
	}
}

void CadCommandBar::_on_input_text_submitted(const String &p_text) {
	if (current_results.size() > 0) {
		TreeItem *selected = results_tree->get_selected();
		if (selected) {
			String command_id = selected->get_metadata(0);
			_execute_command(command_id);
		} else if (current_results.size() > 0) {
			_execute_command(current_results[0].command_id);
		}
	}
	deactivate();
}

void CadCommandBar::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed()) {
		return;
	}

	switch (key->get_keycode()) {
		case Key::ESCAPE: {
			deactivate();
			command_input->accept_event();
		} break;

		case Key::UP: {
			if (results_tree->get_root() && results_tree->get_root()->get_first_child()) {
				TreeItem *selected = results_tree->get_selected();
				if (selected) {
					TreeItem *prev = selected->get_prev_visible();
					if (prev) {
						prev->select(0);
						results_tree->ensure_cursor_is_visible();
						_update_preview(prev->get_metadata(0));
					}
				}
			}
			command_input->accept_event();
		} break;

		case Key::DOWN: {
			if (results_tree->get_root() && results_tree->get_root()->get_first_child()) {
				TreeItem *selected = results_tree->get_selected();
				if (selected) {
					TreeItem *next = selected->get_next_visible();
					if (next) {
						next->select(0);
						results_tree->ensure_cursor_is_visible();
						_update_preview(next->get_metadata(0));
					}
				} else {
					TreeItem *first = results_tree->get_root()->get_first_child();
					if (first) {
						first->select(0);
						_update_preview(first->get_metadata(0));
					}
				}
			}
			command_input->accept_event();
		} break;

		case Key::TAB: {
			_complete_command();
			command_input->accept_event();
		} break;

		default:
			break;
	}
}

void CadCommandBar::_on_result_selected() {
	TreeItem *selected = results_tree->get_selected();
	if (selected) {
		String command_id = selected->get_metadata(0);
		_update_preview(command_id);
	}
}

void CadCommandBar::_on_result_activated() {
	TreeItem *selected = results_tree->get_selected();
	if (selected) {
		String command_id = selected->get_metadata(0);
		_execute_command(command_id);
		deactivate();
	}
}

void CadCommandBar::_on_popup_hide() {
	// 弹窗关闭时不自动停用，允许用户继续输入
}

void CadCommandBar::_position_popup() {
	if (!command_input->is_visible_in_tree()) {
		return;
	}

	Vector2 pos = command_input->get_screen_position();
	pos.y += command_input->get_size().y;

	Size2 popup_size = results_popup->get_contents_minimum_size();
	popup_size.x = MAX(popup_size.x, command_input->get_size().x);

	results_popup->set_position(pos);
	results_popup->set_size(popup_size);
}

void CadCommandBar::_update_search_results(const String &p_search_text) {
	current_results.clear();
	results_tree->clear();
	TreeItem *root = results_tree->create_item();

	// 计算每个命令的匹配分数
	for (const KeyValue<String, CommandInfo> &E : commands) {
		float score = _calculate_match_score(p_search_text, E.value);
		if (score > 0 || p_search_text.is_empty()) {
			SearchResult result;
			result.command_id = E.key;
			result.score = score;

			// 如果搜索为空，使用最近使用时间和使用次数作为分数
			if (p_search_text.is_empty()) {
				result.score = E.value.last_used * 0.001f + E.value.use_count * 0.1f;
			}

			current_results.push_back(result);
		}
	}

	// 排序结果
	current_results.sort();

	// 限制显示数量（使用设置中的值）
	int count = MIN(current_results.size(), max_search_results);

	// 填充树
	for (int i = 0; i < count; i++) {
		const String &cmd_id = current_results[i].command_id;
		const CommandInfo &cmd = commands[cmd_id];

		TreeItem *item = results_tree->create_item(root);
		item->set_text(0, cmd.name);
		item->set_metadata(0, cmd_id);
		item->set_text(1, cmd.shortcut_text);

		if (cmd.icon.is_valid()) {
			item->set_icon(0, cmd.icon);
		}

		// 设置快捷键文本颜色
		item->set_custom_color(1, Color(0.5, 0.8, 1.0));
	}

	// 选中第一个结果
	if (root->get_first_child()) {
		root->get_first_child()->select(0);
		_update_preview(current_results[0].command_id);
	} else {
		preview_name->set_text("");
		preview_description->set_text(TTR("No matching commands found"));
		preview_shortcut->set_text("");
		preview_icon->set_texture(Ref<Texture2D>());
	}
}

void CadCommandBar::_update_preview(const String &p_command_id) {
	if (!commands.has(p_command_id)) {
		return;
	}

	const CommandInfo &cmd = commands[p_command_id];
	preview_name->set_text(cmd.name);
	preview_description->set_text(cmd.description);
	preview_shortcut->set_text(cmd.shortcut_text.is_empty() ? "" : TTR("Shortcut: ") + cmd.shortcut_text);
	preview_icon->set_texture(cmd.icon);
}

void CadCommandBar::_execute_command(const String &p_command_id) {
	if (!commands.has(p_command_id)) {
		return;
	}

	CommandInfo &cmd = commands[p_command_id];

	// 更新使用统计
	cmd.last_used = OS::get_singleton()->get_unix_time();
	cmd.use_count++;

	// 添加到历史记录
	command_history.erase(p_command_id);
	command_history.insert(0, p_command_id);
	if (command_history.size() > max_history_size) {
		command_history.resize(max_history_size);
	}

	// 执行命令
	if (cmd.callback.is_valid()) {
		Variant ret;
		Callable::CallError ce;
		cmd.callback.callp(nullptr, 0, ret, ce);
	}
}

void CadCommandBar::_complete_command() {
	if (current_results.size() > 0) {
		TreeItem *selected = results_tree->get_selected();
		String cmd_id;
		if (selected) {
			cmd_id = selected->get_metadata(0);
		} else {
			cmd_id = current_results[0].command_id;
		}

		if (commands.has(cmd_id)) {
			command_input->set_text(commands[cmd_id].name);
			command_input->set_caret_column(command_input->get_text().length());
		}
	}
}


float CadCommandBar::_calculate_match_score(const String &p_search, const CommandInfo &p_command) {
	if (p_search.is_empty()) {
		return 1.0f;
	}

	String search_lower = p_search.to_lower();
	String name_lower = p_command.name.to_lower();
	String id_lower = p_command.id.to_lower();
	String keywords_lower = p_command.keywords.to_lower();

	float score = 0.0f;

	// 精确匹配名称开头 - 最高分
	if (name_lower.begins_with(search_lower)) {
		score = MAX(score, 1.0f);
	}

	// 精确匹配ID开头
	if (id_lower.begins_with(search_lower)) {
		score = MAX(score, 0.95f);
	}

	// 名称包含搜索词
	int pos = name_lower.find(search_lower);
	if (pos != -1) {
		score = MAX(score, 0.9f - pos * 0.01f);
	}

	// ID包含搜索词
	pos = id_lower.find(search_lower);
	if (pos != -1) {
		score = MAX(score, 0.8f - pos * 0.01f);
	}

	// 关键词匹配
	if (keywords_lower.find(search_lower) != -1) {
		score = MAX(score, 0.7f);
	}

	// 子序列匹配（模糊搜索）
	if (fuzzy_search_enabled && search_lower.is_subsequence_ofn(name_lower)) {
		score = MAX(score, 0.6f);
	}

	// 拼音首字母匹配
	if (pinyin_search_enabled && _match_pinyin_initials(search_lower, p_command.name)) {
		score = MAX(score, 0.75f);
	}

	// 使用频率加成
	if (score > 0) {
		score += p_command.use_count * 0.001f;
		score += (p_command.last_used > 0 ? 0.05f : 0);
	}

	return score;
}

bool CadCommandBar::_match_pinyin_initials(const String &p_search, const String &p_text) {
	String initials = _get_pinyin_initials(p_text);
	return initials.to_lower().find(p_search.to_lower()) != -1;
}

String CadCommandBar::_get_pinyin_initials(const String &p_chinese) {
	String result;

	for (int i = 0; i < p_chinese.length(); i++) {
		char32_t c = p_chinese[i];

		// ASCII字符直接添加
		if (c < 128) {
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
				result += String::chr(c).to_lower();
			}
			continue;
		}

		// 查找拼音首字母
		bool found = false;
		for (int j = 0; PINYIN_TABLE[j][0] != 0; j++) {
			if (PINYIN_TABLE[j][0] == c) {
				result += String::chr(PINYIN_TABLE[j][1]);
				found = true;
				break;
			}
		}

		// 如果没找到，尝试使用Unicode范围估算
		// 这是一个简化的方法，实际应用中应该使用完整的拼音库
		if (!found && c >= 0x4E00 && c <= 0x9FFF) {
			// 中文字符范围，但没有在表中找到
			// 可以扩展拼音表或使用外部库
		}
	}

	return result;
}

void CadCommandBar::_save_history() {
	Dictionary history_data;

	// 保存命令使用统计
	Dictionary command_stats;
	for (const KeyValue<String, CommandInfo> &E : commands) {
		if (E.value.last_used > 0 || E.value.use_count > 0) {
			Dictionary stats;
			stats["last_used"] = E.value.last_used;
			stats["use_count"] = E.value.use_count;
			command_stats[E.key] = stats;
		}
	}
	history_data["command_stats"] = command_stats;

	// 保存历史记录
	Array history_array;
	for (const String &cmd_id : command_history) {
		history_array.push_back(cmd_id);
	}
	history_data["history"] = history_array;

	EditorSettings::get_singleton()->set_project_metadata("cad_command_bar", "history", history_data);
}

void CadCommandBar::_load_history() {
	Dictionary history_data = EditorSettings::get_singleton()->get_project_metadata("cad_command_bar", "history", Dictionary());

	// 加载命令使用统计
	if (history_data.has("command_stats")) {
		Dictionary command_stats = history_data["command_stats"];
		for (const Variant *key = command_stats.next(); key; key = command_stats.next(key)) {
			String cmd_id = *key;
			if (commands.has(cmd_id)) {
				Dictionary stats = command_stats[cmd_id];
				if (stats.has("last_used")) {
					commands[cmd_id].last_used = stats["last_used"];
				}
				if (stats.has("use_count")) {
					commands[cmd_id].use_count = stats["use_count"];
				}
			}
		}
	}

	// 加载历史记录
	if (history_data.has("history")) {
		Array history_array = history_data["history"];
		command_history.clear();
		for (int i = 0; i < history_array.size() && i < max_history_size; i++) {
			String cmd_id = history_array[i];
			if (commands.has(cmd_id)) {
				command_history.push_back(cmd_id);
			}
		}
	}
}

void CadCommandBar::load_settings() {
	// 从编辑器设置加载命令栏配置
	max_history_size = EDITOR_GET("editors/3d/command_bar/max_history_size");
	max_search_results = EDITOR_GET("editors/3d/command_bar/max_search_results");
	show_shortcut_hints = EDITOR_GET("editors/3d/command_bar/show_shortcut_hints");
	fuzzy_search_enabled = EDITOR_GET("editors/3d/command_bar/fuzzy_search");
	pinyin_search_enabled = EDITOR_GET("editors/3d/command_bar/pinyin_search");

	// 根据设置更新UI
	if (preview_shortcut) {
		preview_shortcut->set_visible(show_shortcut_hints);
	}
}

void CadCommandBar::register_command(const String &p_id, const String &p_name, const String &p_description,
		const Callable &p_callback, const Ref<Texture2D> &p_icon,
		const String &p_keywords, const String &p_shortcut_text) {
	CommandInfo cmd;
	cmd.id = p_id;
	cmd.name = p_name;
	cmd.description = p_description;
	cmd.callback = p_callback;
	cmd.icon = p_icon;
	cmd.keywords = p_keywords;
	cmd.shortcut_text = p_shortcut_text;

	commands[p_id] = cmd;
}

void CadCommandBar::unregister_command(const String &p_id) {
	commands.erase(p_id);
}

void CadCommandBar::clear_commands() {
	commands.clear();
}

void CadCommandBar::collect_editor_commands() {
	if (!spatial_editor) {
		return;
	}

	// 注册3D编辑器工具命令
	// Transform Mode
	register_command("tool_transform", TTR("Transform Mode"), TTR("Switch to transform mode for manipulating objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TOOL_TRANSFORM),
			get_theme_icon(SNAME("ToolTransform"), EditorStringName(EditorIcons)),
			"transform bianhuang gj", "Q");

	// Move Mode
	register_command("tool_move", TTR("Move Mode"), TTR("Switch to move mode for translating objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TOOL_MOVE),
			get_theme_icon(SNAME("ToolMove"), EditorStringName(EditorIcons)),
			"move yidong yd", "W");

	// Rotate Mode
	register_command("tool_rotate", TTR("Rotate Mode"), TTR("Switch to rotate mode for rotating objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TOOL_ROTATE),
			get_theme_icon(SNAME("ToolRotate"), EditorStringName(EditorIcons)),
			"rotate xuanzhuan xz", "E");

	// Scale Mode
	register_command("tool_scale", TTR("Scale Mode"), TTR("Switch to scale mode for scaling objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TOOL_SCALE),
			get_theme_icon(SNAME("ToolScale"), EditorStringName(EditorIcons)),
			"scale suofang sf", "R");

	// Select Mode
	register_command("tool_select", TTR("Select Mode"), TTR("Switch to select mode for selecting objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TOOL_SELECT),
			get_theme_icon(SNAME("ToolSelect"), EditorStringName(EditorIcons)),
			"select xuanze xz", "V");

	// Local Coords
	register_command("local_coords", TTR("Use Local Space"), TTR("Toggle between local and global coordinate space"),
			callable_mp(spatial_editor, &Node3DEditor::toggle_menu_item).bind(true, Node3DEditor::MENU_TOOL_LOCAL_COORDS),
			get_theme_icon(SNAME("Object"), EditorStringName(EditorIcons)),
			"local coords bendizuobiao bdzb", "T");

	// Snap
	register_command("use_snap", TTR("Use Snap"), TTR("Toggle snapping for precise positioning"),
			callable_mp(spatial_editor, &Node3DEditor::toggle_menu_item).bind(true, Node3DEditor::MENU_TOOL_USE_SNAP),
			get_theme_icon(SNAME("Snap"), EditorStringName(EditorIcons)),
			"snap xifu xf", "Y");

	// Ruler Mode
	register_command("ruler", TTR("Ruler Mode"), TTR("Measure distance between two points in 3D space"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_RULER),
			get_theme_icon(SNAME("Ruler"), EditorStringName(EditorIcons)),
			"ruler chizi cz celiang cl", "M");

	// Lock Selected
	register_command("lock_selected", TTR("Lock Selected"), TTR("Lock the selected objects to prevent accidental modification"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_LOCK_SELECTED),
			get_theme_icon(SNAME("Lock"), EditorStringName(EditorIcons)),
			"lock suoding sd", "");

	// Unlock Selected
	register_command("unlock_selected", TTR("Unlock Selected"), TTR("Unlock the selected objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_UNLOCK_SELECTED),
			get_theme_icon(SNAME("Unlock"), EditorStringName(EditorIcons)),
			"unlock jiesuo js", "");

	// Group Selected
	register_command("group_selected", TTR("Group Selected"), TTR("Group the selected objects together"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_GROUP_SELECTED),
			get_theme_icon(SNAME("Group"), EditorStringName(EditorIcons)),
			"group zuhe zh", "");

	// Ungroup Selected
	register_command("ungroup_selected", TTR("Ungroup Selected"), TTR("Ungroup the selected objects"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_UNGROUP_SELECTED),
			get_theme_icon(SNAME("Ungroup"), EditorStringName(EditorIcons)),
			"ungroup quxiaozuhe qxzh", "");

	// Snap to Floor
	register_command("snap_to_floor", TTR("Snap to Floor"), TTR("Snap selected objects to the floor/ground"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_SNAP_TO_FLOOR),
			get_theme_icon(SNAME("SnapGrid"), EditorStringName(EditorIcons)),
			"snap floor xifudimian xfdm", "");

	// Configure Snap
	register_command("configure_snap", TTR("Configure Snap..."), TTR("Open snap configuration dialog"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TRANSFORM_CONFIGURE_SNAP),
			get_theme_icon(SNAME("Snap"), EditorStringName(EditorIcons)),
			"configure snap peizhi pz", "");

	// Transform Dialog
	register_command("transform_dialog", TTR("Transform Dialog..."), TTR("Open transform dialog for precise transformations"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_TRANSFORM_DIALOG),
			get_theme_icon(SNAME("Transform"), EditorStringName(EditorIcons)),
			"transform dialog bianhuanduihuakuang bhdhk", "");

	// View Options
	register_command("view_origin", TTR("View Origin"), TTR("Toggle visibility of the origin marker"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_ORIGIN),
			get_theme_icon(SNAME("Gizmo"), EditorStringName(EditorIcons)),
			"origin yuandian yd", "");

	register_command("view_grid", TTR("View Grid"), TTR("Toggle visibility of the grid"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_GRID),
			get_theme_icon(SNAME("Grid"), EditorStringName(EditorIcons)),
			"grid wangge wg", "");

	// Viewport layouts
	register_command("view_1_viewport", TTR("1 Viewport"), TTR("Use single viewport layout"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_USE_1_VIEWPORT),
			Ref<Texture2D>(), "viewport 1 shitu st", "");

	register_command("view_2_viewports", TTR("2 Viewports"), TTR("Use two viewport layout"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_USE_2_VIEWPORTS),
			Ref<Texture2D>(), "viewport 2 shitu st", "");

	register_command("view_4_viewports", TTR("4 Viewports"), TTR("Use four viewport layout"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_USE_4_VIEWPORTS),
			Ref<Texture2D>(), "viewport 4 shitu st", "");

	// Camera Settings
	register_command("camera_settings", TTR("Camera Settings..."), TTR("Open camera settings dialog"),
			callable_mp(spatial_editor, &Node3DEditor::execute_menu_item).bind(Node3DEditor::MENU_VIEW_CAMERA_SETTINGS),
			get_theme_icon(SNAME("Camera3D"), EditorStringName(EditorIcons)),
			"camera settings xiangjishezhi xjsz", "");
}
