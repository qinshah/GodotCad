/**************************************************************************/
/*  cad_command_bar.h                                                     */
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

#pragma once

#include "core/input/shortcut.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"

class Node3DEditor;

// CAD风格命令栏 - 用于3D编辑器的快速命令输入和搜索
class CadCommandBar : public HBoxContainer {
	GDCLASS(CadCommandBar, HBoxContainer);

public:
	// 命令信息结构
	struct CommandInfo {
		String id;              // 命令唯一标识符
		String name;            // 显示名称
		String description;     // 命令描述
		String keywords;        // 搜索关键词（包括拼音首字母）
		String shortcut_text;   // 快捷键文本
		Ref<Texture2D> icon;    // 命令图标
		Callable callback;      // 执行回调
		int last_used = 0;      // 最后使用时间戳
		int use_count = 0;      // 使用次数
	};

private:
	// UI组件
	LineEdit *command_input = nullptr;
	Button *toggle_button = nullptr;
	PopupPanel *results_popup = nullptr;
	Tree *results_tree = nullptr;
	PanelContainer *preview_panel = nullptr;
	TextureRect *preview_icon = nullptr;
	Label *preview_name = nullptr;
	Label *preview_description = nullptr;
	Label *preview_shortcut = nullptr;

	// 命令注册表
	HashMap<String, CommandInfo> commands;
	Vector<String> command_history;
	int max_history_size = 50;

	// 状态
	bool is_active = false;
	int selected_index = -1;
	Node3DEditor *spatial_editor = nullptr;

	// 设置
	int max_search_results = 20;
	bool show_shortcut_hints = true;
	bool fuzzy_search_enabled = true;
	bool pinyin_search_enabled = true;

	// 搜索结果缓存
	struct SearchResult {
		String command_id;
		float score;
		bool operator<(const SearchResult &other) const {
			return score > other.score; // 降序排列
		}
	};
	Vector<SearchResult> current_results;

	// 内部方法
	void _on_input_text_changed(const String &p_text);
	void _on_input_text_submitted(const String &p_text);
	void _on_input_gui_input(const Ref<InputEvent> &p_event);
	void _on_toggle_pressed();
	void _on_result_selected();
	void _on_result_activated();
	void _on_popup_hide();

	void _update_search_results(const String &p_search_text);
	void _update_preview(const String &p_command_id);
	void _execute_command(const String &p_command_id);
	void _complete_command();

	float _calculate_match_score(const String &p_search, const CommandInfo &p_command);
	bool _match_pinyin_initials(const String &p_search, const String &p_text);
	String _get_pinyin_initials(const String &p_chinese);

	void _save_history();
	void _load_history();

	void _position_popup();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// 公共接口
	void activate();
	void deactivate();
	bool is_command_bar_active() const { return is_active; }

	// 命令注册
	void register_command(const String &p_id, const String &p_name, const String &p_description,
			const Callable &p_callback, const Ref<Texture2D> &p_icon = Ref<Texture2D>(),
			const String &p_keywords = "", const String &p_shortcut_text = "");
	void unregister_command(const String &p_id);
	void clear_commands();

	// 从Node3DEditor收集命令
	void collect_editor_commands();

	// 设置
	void set_spatial_editor(Node3DEditor *p_editor) { spatial_editor = p_editor; }
	void set_max_history_size(int p_size) { max_history_size = p_size; }
	int get_max_history_size() const { return max_history_size; }

	// 从编辑器设置加载配置
	void load_settings();

	CadCommandBar();
	~CadCommandBar();
};
