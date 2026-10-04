/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file field_widget.h Types related to the farm field widgets. */

#ifndef WIDGETS_FIELD_WIDGET_H
#define WIDGETS_FIELD_WIDGET_H

/** Widgets of the #BuildFarmToolbarWindow class. */
enum FarmToolbarWidgets : WidgetID {
	WID_FT_FIELD, ///< Build field button.
	WID_FT_DEMOLISH, ///< Demolish button.
};

/** Widgets of the #FieldViewWindow class. */
enum FieldViewWidgets : WidgetID {
	WID_FV_CAPTION, ///< Caption of the window.
	WID_FV_LOCATION, ///< Centre the main view on the field.
	WID_FV_INFO, ///< Field status text.
	WID_FV_TASKS, ///< Task list.
	WID_FV_SCROLLBAR, ///< Scrollbar of the task list.
	WID_FV_ADD, ///< Add task dropdown.
	WID_FV_DELETE, ///< Delete selected task.
	WID_FV_MONTH, ///< Start month dropdown.
	WID_FV_GOTO, ///< Make the selected task current.
	WID_FV_DO_NOW, ///< Do the current task instantly (testing aid).
	WID_FV_SOW, ///< Crop to sow dropdown.
};

#endif /* WIDGETS_FIELD_WIDGET_H */
