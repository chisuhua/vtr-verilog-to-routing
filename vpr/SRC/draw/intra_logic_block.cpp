/* This file holds subroutines responsible for drawing inside clustered logic blocks.
 * The four main subroutines defined here are draw_internal_alloc_blk(), 
 * draw_internal_init_blk(), draw_internal_draw_subblk(), and toggle_blk_internal().
 * When VPR graphics initially sets up, draw_internal_alloc_blk() will be called from 
 * draw.c to allocate space for the structures needed for internal blks drawing.
 * Before any drawing, draw_internal_init_blk() will pre-compute a bounding box
 * for each sub-block in the pb_graph of every physical block type. When the menu button 
 * "Blk Internal" is pressed, toggle_blk_internal() will enable internal blocks drawing.
 * Then, with each subsequent click on the button, toggle_blk_internal() will propel one 
 * more level of pbs to be drawn. Draw_internal_draw_subblk() will be called whenever 
 * new blocks need to be drawn, and this function is responsible for drawing sub-blocks 
 * from the pre-computed bounding box values.
 * 
 * Author: Long Yu (Mike) Wang
 * Date: August 2013, May 2014
 *
 * Author: Matthew J.P. Walker
 * Date: May 2014
 */

#include <cstdio>
#include <algorithm>
#include "vtr_assert.h"
#include <string.h>
using namespace std;

#include "intra_logic_block.h"
#include "globals.h"
#include "atom_netlist.h"
#include "vpr_utils.h"
#include "draw_global.h"
#include "graphics.h"
#include "draw.h"

/************************* Subroutines local to this file. *******************************/

static void draw_internal_load_coords(int type_descrip_index, t_pb_graph_node *pb_graph_node, 
									  float parent_width, float parent_height);
static void draw_internal_calc_coords(int type_descrip_index, t_pb_graph_node *pb_graph_node, 
									  int num_pb_types, int type_index, int num_pb, int pb_index, 
									  float parent_width, float parent_height, 
									  float *blk_width, float *blk_height);
static int draw_internal_find_max_lvl(t_pb_type pb_type);
static void draw_internal_pb(const t_block* const clb, t_pb* pb, const t_bound_box& parent_bbox);
static bool is_top_lvl_block_highlighted(const t_block& clb);

void draw_one_logical_connection(const AtomPinId src_pin, const AtomPinId sink_pin);
t_pb* highlight_sub_block_helper(const t_block& clb, t_pb* pb, const t_point& local_pt, int max_depth);

/************************* Subroutine definitions begin *********************************/

void draw_internal_alloc_blk() {
	t_draw_coords *draw_coords;
	int i;
	t_pb_graph_node *pb_graph_head;
	
	/* Call accessor function to retrieve global variables. */
	draw_coords = get_draw_coords_vars();
	
	/* Create a vector holding coordinate information for each type of physical logic
	 * block.
	 */
	draw_coords->blk_info.resize(num_types);

	for (i = 0; i < num_types; ++i) {
		/* Empty block has no sub_blocks */
		if (&type_descriptors[i] == EMPTY_TYPE)
			continue;
		
		pb_graph_head = type_descriptors[i].pb_graph_head;
		
		/* Create an vector with size equal to the total number of pins for each type 
		 * of physical logic block, in order to uniquely identify each sub-block in 
		 * the pb_graph of that type.
		 */
		draw_coords->blk_info.at(i).subblk_array.resize(pb_graph_head->total_pb_pins);
	}
}


void draw_internal_init_blk() {
	/* Call accessor function to retrieve global variables. */
	t_draw_coords* draw_coords = get_draw_coords_vars();
	t_draw_state* draw_state = get_draw_state_vars();

	t_pb_graph_node *pb_graph_head_node;

	for (int i = 0; i < num_types; ++i) {
		/* Empty block has no sub_blocks */
		s_type_descriptor& type_desc = type_descriptors[i];
		if (&type_desc == EMPTY_TYPE)
			continue;

		pb_graph_head_node = type_desc.pb_graph_head;
		int type_descriptor_index = type_desc.index;

		int num_sub_tiles = type_desc.capacity;


		// set the clb dimensions
		t_bound_box& clb_bbox = draw_coords->blk_info.at(type_descriptor_index).subblk_array.at(0);

		// note, that all clbs of the same type are the same size,
		// and that consequently we have *one* model for each type.
		clb_bbox.bottom_left() = t_point(0,0);
		if (type_desc.width > (nx + 2) || type_desc.height > (ny + 2)) {
			// in this case, the clb certainly wont't fit, but this prevents
			// an out-of-bounds access, and provides some sort of (probably right)
			// value
			clb_bbox.top_right() = t_point(
				(draw_coords->tile_x[1]-draw_coords->tile_x[0])*(type_desc.width - 1),
				(draw_coords->tile_y[1]-draw_coords->tile_y[0])*(type_desc.height - 1)
			);
		} else {
			clb_bbox.top_right() = t_point(
				draw_coords->tile_x[type_desc.width  - 1],
				draw_coords->tile_y[type_desc.height - 1]
			);
		}
		clb_bbox.top_right() += t_point(
			draw_coords->get_tile_width()/num_sub_tiles,
			draw_coords->get_tile_width()
		);

		draw_internal_load_coords(type_descriptor_index, pb_graph_head_node, 
								  clb_bbox.get_width(), clb_bbox.get_height());

		/* Determine the max number of sub_block levels in the FPGA */
		draw_state->max_sub_blk_lvl = max(draw_internal_find_max_lvl(*type_desc.pb_type),
							   draw_state->max_sub_blk_lvl);
	}
}



void draw_internal_draw_subblk() {
	for (int i = 0; i <= (nx + 1); i++) {
		for (int j = 0; j <= (ny + 1); j++) {
			/* Only the first block of a group should control drawing */
			if (grid[i][j].width_offset > 0 || grid[i][j].height_offset > 0) 
				continue;

			/* Don't draw if tile is empty. This includes corners. */
			if (grid[i][j].type == EMPTY_TYPE)
				continue;

			int num_sub_tiles = grid[i][j].type->capacity;
			for (int k = 0; k < num_sub_tiles; ++k) {
				/* Don't draw if block is empty. */
				if (grid[i][j].blocks[k] == EMPTY_BLOCK || grid[i][j].blocks[k] == INVALID_BLOCK)
					continue;

				/* Get block ID */
				int bnum = grid[i][j].blocks[k];
				/* Safety check, that physical blocks exists in the CLB */
				if (block[bnum].pb == NULL)
					continue;

				draw_internal_pb(&block[bnum], block[bnum].pb, t_bound_box(0,0,0,0));
			}
		}
	}
}


/* This function traverses through the pb_graph of a certain physical block type and 
 * finds the maximum sub-block levels for that type.
 */
static int draw_internal_find_max_lvl(t_pb_type pb_type) {
	int i, j;
	t_mode mode;
	int max_levels = 0;

	/* If no modes, we have reached the end of pb_graph */
	if (pb_type.num_modes == 0)
		return (pb_type.depth);

	for (i = 0; i < pb_type.num_modes; ++i) {
		mode = pb_type.modes[i];

		for (j = 0; j < mode.num_pb_type_children; ++j) {
			max_levels = max(draw_internal_find_max_lvl(mode.pb_type_children[j]), max_levels);
		}
	}

	return max_levels;
}


/* Helper function for initializing bounding box values for each sub-block. This function
 * traverses through the pb_graph for a descriptor_type (given by type_descrip_index), and
 * calls helper function to compute bounding box values.
 */
static void draw_internal_load_coords(int type_descrip_index, t_pb_graph_node *pb_graph_node, 
									  float parent_width, float parent_height) {
	int i, j, k;
	t_pb_type *pb_type;
	int num_modes, num_children, num_pb;
	t_mode mode;
	float blk_width = 0.;
	float blk_height = 0.;

	/* Get information about the pb_type */
	pb_type = pb_graph_node->pb_type;
	num_modes = pb_type->num_modes;

	/* If no modes, we have reached the end of pb_graph */
	if (num_modes == 0)
		return;

	for (i = 0; i < num_modes; ++i) {
		mode = pb_type->modes[i];
		num_children = mode.num_pb_type_children;

		for (j = 0; j < num_children; ++j) {
			/* Find the number of instances for each child pb_type. */
			num_pb = mode.pb_type_children[j].num_pb;

			for (k = 0; k < num_pb; ++k) { 
				/* Compute bound box for block. Don't call if pb_type is root-level pb. */
				draw_internal_calc_coords(type_descrip_index, 
										  &pb_graph_node->child_pb_graph_nodes[i][j][k], 
										  num_children, j, num_pb, k,
										  parent_width, parent_height, 
										  &blk_width, &blk_height);		

				/* Traverse to next level in the pb_graph */
				draw_internal_load_coords(type_descrip_index, 
										  &pb_graph_node->child_pb_graph_nodes[i][j][k], 
										  blk_width, blk_height);
			}
		}
	}
	return;
}


/* Helper function which computes bounding box values for a sub-block. The coordinates 
 * are relative to the left and bottom corner of the parent block.
 */
static void 
draw_internal_calc_coords(int type_descrip_index, t_pb_graph_node *pb_graph_node, 
						  int num_pb_types, int type_index, int num_pb, int pb_index, 
						  float parent_width, float parent_height, 
						  float *blk_width, float *blk_height) 
{
	float parent_drawing_width, parent_drawing_height;
	float sub_tile_x, sub_tile_y;
	float child_width, child_height;
	// get the bbox for this pb type
	t_bound_box& pb_bbox = get_draw_coords_vars()->blk_info.at(type_descrip_index).get_pb_bbox_ref(*pb_graph_node);

	const float FRACTION_PARENT_PADDING_X = 0.01;

	const float NORMAL_FRACTION_PARENT_HEIGHT = 0.90;
	      float capacity_divisor = 1;
	const float FRACTION_PARENT_PADDING_BOTTOM = 0.01;

	const float FRACTION_CHILD_MARGIN_X = 0.025;
	const float FRACTION_CHILD_MARGIN_Y = 0.04;

	int capacity = type_descriptors[type_descrip_index].capacity;
	if (capacity > 1 && nx > 0 && ny > 0 && grid[1][0].usage != 0
		&& type_descrip_index == grid[1][0].type->index) {

		// that should test for io blocks, and setting capacity_divisor > 1
		// will squish every thing down
		capacity_divisor = capacity - 1;
	}

	/* Draw all child-level blocks in just most of the space inside their parent block. */
	parent_drawing_width = parent_width * (1 - FRACTION_PARENT_PADDING_X*2);
	parent_drawing_height = parent_height * (NORMAL_FRACTION_PARENT_HEIGHT / capacity_divisor);


	/* The left and bottom corner (inside the parent block) of the space to draw 
	 * child blocks. 
	 */
	sub_tile_x = parent_width * FRACTION_PARENT_PADDING_X;
	sub_tile_y = parent_height * FRACTION_PARENT_PADDING_BOTTOM;

	/* Divide parent_drawing_width by the number of child types. */
	child_width = parent_drawing_width/num_pb_types;
	/* Divide parent_drawing_height by the number of instances of the pb_type. */
	child_height = parent_drawing_height/num_pb;

	/* The starting point to draw the physical block. */
	pb_bbox.left()   = child_width * type_index + sub_tile_x + FRACTION_CHILD_MARGIN_X * child_width;
	pb_bbox.bottom() = child_height * pb_index  + sub_tile_y + FRACTION_CHILD_MARGIN_Y * child_height;

	/* Leave some space between different pb_types. */
	child_width *= 1 - FRACTION_CHILD_MARGIN_X*2;
	/* Leave some space between different instances of the same type. */
	child_height *= 1 - FRACTION_CHILD_MARGIN_Y*2;

	/* Endpoint for drawing the pb_type */
	pb_bbox.right() = pb_bbox.left()   + child_width;
	pb_bbox.top()   = pb_bbox.bottom() + child_height;

	*blk_width = child_width;
	*blk_height = child_height;

	return;
}

/* Helper subroutine to draw all sub-blocks. This function traverses through the pb_graph 
 * which a netlist block can map to, and draws each sub-block inside its parent block. With
 * each click on the "Blk Internal" button, a new level is shown. 
 */
static void draw_internal_pb(const t_block* const clb, t_pb* pb, const t_bound_box& parent_bbox) {
	t_draw_coords* draw_coords = get_draw_coords_vars();
	t_draw_state* draw_state = get_draw_state_vars();
	t_selected_sub_block_info& sel_sub_info = get_selected_sub_block_info();

	t_pb_type* pb_type = pb->pb_graph_node->pb_type;
	t_bound_box abs_bbox = draw_coords->get_pb_bbox(*clb, *pb->pb_graph_node) + parent_bbox.bottom_left();

	// if we've gone too far, don't draw anything
	if (pb_type->depth > draw_state->show_blk_internal) {
		return;
	}

	/// first draw box ///

	if (pb_type->depth == 0) {
		if (!is_top_lvl_block_highlighted(*clb)) {
			// if this is a top level pb, and only if it isn't selected (ie. a funny colour),
			// overwrite it. (but stil draw the text)
			setcolor(WHITE);
			fillrect(abs_bbox); 
			setcolor(BLACK);
			setlinestyle(SOLID);
			drawrect(abs_bbox);
		}
	} else {
		if (pb->name != NULL) {
			// If block is used, draw it in colour with solid border.

			setlinestyle(SOLID);

			// type_index indicates what type of block.
			const int type_index = clb->type->index;

			// determine default background color
			if (sel_sub_info.is_selected(pb->pb_graph_node, clb)) {
				setcolor(SELECTED_COLOR);
			} else if (sel_sub_info.is_head_of_critical_path(pb->pb_graph_node, clb)) {
				setcolor(crit_path_colors::blk::HEAD);
			} else if (sel_sub_info.is_driver_of_head_of_critical_path(pb->pb_graph_node, clb)) {
				setcolor(crit_path_colors::blk::HEAD_DRIVER);
			} else if (sel_sub_info.is_on_critical_path(pb->pb_graph_node, clb)) {
				setcolor(crit_path_colors::blk::TAIL);
			} else if (sel_sub_info.is_sink_of_selected(pb->pb_graph_node, clb)) {
				setcolor(DRIVES_IT_COLOR);
			} else if (sel_sub_info.is_source_of_selected(pb->pb_graph_node, clb)) {
				setcolor(DRIVEN_BY_IT_COLOR);
			} else if (pb_type->depth != draw_state->show_blk_internal && pb->child_pbs != NULL) {
				setcolor(WHITE); // draw anthing else that will have a child as white
			} else if (type_index < 3) {
				setcolor(LIGHTGREY);
			} else if (type_index < 3 + MAX_BLOCK_COLOURS) {
				setcolor(BISQUE + MAX_BLOCK_COLOURS + type_index - 3);
			} else {
				setcolor(BISQUE + 2 * MAX_BLOCK_COLOURS - 1);
			}
		}
		else {
			// If block is not used, draw as empty block (ie. white
			// background with dashed border).

			setlinestyle(DASHED);
			setcolor(WHITE);
		}

		fillrect(abs_bbox);
		setcolor(BLACK);
		drawrect(abs_bbox);
	}

	/// then draw text ///

	if (pb->name != NULL) {
		setfontsize(16); // note: calc_text_xbound(...) assumes this is 16
		if (pb_type->depth == draw_state->show_blk_internal || pb->child_pbs == NULL) {
			// If this pb is at the lowest displayed level, or has no more children, then
			// label it in the center with its type and name

			int type_len = strlen(pb_type->name);
			int name_len = strlen(pb->name);
			int tot_len = type_len + name_len;
			char* blk_tag = (char *)vtr::malloc((tot_len + 8) * sizeof(char));

			sprintf (blk_tag, "%s(%s)", pb_type->name, pb->name);

			drawtext(
				t_point(abs_bbox.get_xcenter(), abs_bbox.get_ycenter()),
				blk_tag,
				abs_bbox
			);

			free(blk_tag);
		} else {
			// else (ie. has chilren, and isn't at the lowest displayed level)
			// just label its type, and put it up at the top so we can see it
			drawtext(
				t_point(
					abs_bbox.get_xcenter(),
					abs_bbox.top() - (abs_bbox.get_height()) / 15.0
				),
				pb_type->name,
				abs_bbox
			);
		}
	} else {
		// If child block is not used, label it only by its type
		drawtext(
			t_point(abs_bbox.get_xcenter(), abs_bbox.get_ycenter()),
			pb_type->name,
			abs_bbox
		);
	}

	/// now recurse on the child pbs. ///

	// return if no children, or this is an unusused pb,
	// or if going down will be too far down (this one is redundant, but for optimazition)
	if(pb->child_pbs == NULL || pb->name == NULL
		|| pb_type->depth == draw_state->show_blk_internal) {

		return;
	}

	int num_child_types = pb->get_num_child_types();
	for (int i = 0; i < num_child_types; ++i) {

		if (pb->child_pbs[i] == NULL) {
			continue;
		}

		int num_pb = pb->get_num_children_of_type(i);
		for (int j = 0; j < num_pb; ++j) {

			t_pb* child_pb = &pb->child_pbs[i][j];

			VTR_ASSERT(child_pb != NULL);

			t_pb_type* pb_child_type = child_pb->pb_graph_node->pb_type;

			// don't go farther if 0 modes
			if (pb_child_type == NULL && pb_child_type->num_modes == 0) {
				continue;
			}

			// now recurse
			draw_internal_pb(clb, child_pb, abs_bbox);

		}
	}
}

void draw_logical_connections() {
	const t_selected_sub_block_info& sel_subblk_info = get_selected_sub_block_info();
	t_draw_state* draw_state = get_draw_state_vars();

	// iterate over all the atom nets
    for(auto net_id : g_atom_nl.nets()) {

        AtomPinId driver_pin_id = g_atom_nl.net_driver(net_id);
        AtomBlockId src_blk_id = g_atom_nl.pin_block(driver_pin_id);
		const t_pb_graph_node* src_pb_gnode = g_atom_lookup.atom_pb_graph_node(src_blk_id);
		t_block* src_clb = &block[g_atom_lookup.atom_clb(src_blk_id)];
		bool src_is_selected = sel_subblk_info.is_in_selected_subtree(src_pb_gnode, src_clb);
		bool src_is_src_of_selected = sel_subblk_info.is_source_of_selected(src_pb_gnode, src_clb);

		// iterate over the sinks
        for(auto sink_pin_id : g_atom_nl.net_sinks(net_id)) {

            AtomBlockId sink_blk_id = g_atom_nl.pin_block(sink_pin_id);
            const t_pb_graph_node* sink_pb_gnode = g_atom_lookup.atom_pb_graph_node(sink_blk_id);
			t_block* sink_clb = &block[g_atom_lookup.atom_clb(sink_blk_id)];

			if (sel_subblk_info.is_head_net_of_critical_path(driver_pin_id, sink_pin_id)) {
				setcolor(crit_path_colors::net::HEAD);
			} else if (sel_subblk_info.is_on_critical_path(driver_pin_id, sink_pin_id)) {
				setcolor(crit_path_colors::net::TAIL);
			} else if (src_is_selected && sel_subblk_info.is_sink_of_selected(sink_pb_gnode, sink_clb)) {
				setcolor(DRIVES_IT_COLOR);
			} else if (src_is_src_of_selected && sel_subblk_info.is_in_selected_subtree(sink_pb_gnode, sink_clb)) {
				setcolor(DRIVEN_BY_IT_COLOR);
			} else if (draw_state->show_nets == DRAW_LOGICAL_CONNECTIONS &&
				(draw_state->showing_sub_blocks() || src_clb != sink_clb)) {
				setcolor(BLACK); // if showing all, draw the other ones in black
			} else {
				continue; // not showing all, and not the sperified block, so skip
			}

			draw_one_logical_connection(driver_pin_id, sink_pin_id);		
		}
	}
}

/**
 * Helper function for draw_one_logical_connection(...). 
 * We return the index of the pin in the context of the *model*
 * This is used to determine where to draw connection start/end points
 *
 * The pin index in the model context is returned via pin_index.
 *
 * The total number of model pins (either inputs or outputs) is returned via total_pins
 *
 * The search inputs parameter tells this function to search
 * inputs (if true) or outputs (if false).
 */
void find_pin_index_at_model_scope(
	const AtomPinId pin_id, const AtomBlockId blk_id,
	int* pin_index, int* total_pins) {

    AtomPortId port_id = g_atom_nl.pin_port(pin_id);
    const t_model_ports* model_port = g_atom_nl.port_model(port_id);

    //Total up the port widths
    //  Note that we do this on the model since the atom netlist doesn't include unused ports
    int pin_cnt = 0;
    *pin_index = -1; //initialize
    const t_model* model = g_atom_nl.block_model(blk_id);
    for (const t_model_ports* port : {model->inputs, model->outputs}) {
        while(port) {

            if(port == model_port) {
                //This is the port the pin is associated with, record it's index

                //Get the pin index in the port
                int atom_port_index = g_atom_nl.pin_port_bit(pin_id);

                //The index of this pin in the model is the pins counted so-far 
                //(i.e. accross previous ports) plus the index in the port
                *pin_index = pin_cnt + atom_port_index;
            }

            //Running total of model pins seen so-far
            pin_cnt += port->size;

            port = port->next;
        }
    }

    VTR_ASSERT(*pin_index != -1);

    *total_pins = pin_cnt;
}

/**
 * Draws ONE logical connection from src_pin in src_lblk to sink_pin in sink_lblk.
 * The *_abs_bbox parameters are for mild optmization, as the absolute bbox can be calculated
 * more effeciently elsewhere.
 */
void draw_one_logical_connection(const AtomPinId src_pin, const AtomPinId sink_pin) {

    t_point src_point = atom_pin_draw_coord(src_pin);
    t_point sink_point = atom_pin_draw_coord(sink_pin);

	// draw a link connecting the pins.
	drawline(src_point.x, src_point.y,
		sink_point.x, sink_point.y);

	if (g_atom_lookup.atom_clb(g_atom_nl.pin_block(src_pin)) == g_atom_lookup.atom_clb(g_atom_nl.pin_block(sink_pin))) {
		// if they are in the same clb, put one arrow in the center
		float center_x = (src_point.x + sink_point.x) / 2;
		float center_y = (src_point.y + sink_point.y) / 2;

		draw_triangle_along_line(
			center_x, center_y,
			src_point.x, sink_point.x,
			src_point.y, sink_point.y
		);
	} else {
		// if they are not, put 2 near each end
		draw_triangle_along_line(src_point, sink_point, 0.05);
		draw_triangle_along_line(src_point, sink_point, 0.95);
	}

}

/* This function checks whether a top-level clb has been highlighted. It does 
 * so by checking whether the color in this block is default color.
 */
static bool is_top_lvl_block_highlighted(const t_block& clb) {
	t_draw_state *draw_state;
	ptrdiff_t blk_id = &clb - block;

	/* Call accessor function to retrieve global variables. */
	draw_state = get_draw_state_vars();

	if (clb.type->index < 3) {
		if (draw_state->block_color[blk_id] == LIGHTGREY)
			return false;
	} else if (clb.type->index < 3 + MAX_BLOCK_COLOURS) {
		if (draw_state->block_color[blk_id] == (color_types) (BISQUE + MAX_BLOCK_COLOURS 
												+ clb.type->index - 3))
			return false;
	} else {
		if (draw_state->block_color[blk_id] == (color_types) (BISQUE + 2 * MAX_BLOCK_COLOURS - 1))
			return false;
	}

	return true;
}

int highlight_sub_block(const t_point& point_in_clb, t_block& clb) {
	t_draw_state* draw_state = get_draw_state_vars();

	int max_depth = draw_state->show_blk_internal;

	t_pb* new_selected_sub_block = 
		highlight_sub_block_helper(clb, clb.pb, point_in_clb, max_depth);
	if (new_selected_sub_block == NULL) {
		get_selected_sub_block_info().clear();
		return 1;
	} else {
		get_selected_sub_block_info().set(new_selected_sub_block, &clb);
		return 0;
	}
}

/**
 * The recursive part of highlight_sub_block. finds which pb is under
 * the given location. local_pt is relative to the given pb, and pb should
 * be in clb.
 */
t_pb* highlight_sub_block_helper(
	const t_block& clb, t_pb* pb, const t_point& local_pt, int max_depth) {
	
	t_draw_coords *draw_coords = get_draw_coords_vars();
	t_pb_type* pb_type = pb->pb_graph_node->pb_type;

	// check to see if we are past the displayed level,
	// if pb has children,
	// and if pb is dud
	if (pb_type->depth + 1 > max_depth
	 || pb->child_pbs == NULL
	 || pb_type->num_modes == 0) {
		return NULL;
	}

	int num_child_types = pb->get_num_child_types();

	// Iterate through each type of child pb, and each pb of that type.
	for (int i = 0; i < num_child_types; ++i) {
		int num_children_of_type = pb->get_num_children_of_type(i);

		for (int j = 0; j < num_children_of_type; ++j) {

			if (pb->child_pbs[i] == NULL) {
				continue;
			}

			t_pb* child_pb = &pb->child_pbs[i][j];
			t_pb_graph_node* pb_child_node = child_pb->pb_graph_node;

			// get the bbox for this child
			const t_bound_box& bbox = draw_coords->get_pb_bbox(clb, *pb_child_node);

			// If child block is being used, check if it intersects
			if (child_pb->name != NULL && bbox.intersects(local_pt)) {

				// check farther down the graph, see if we can find
				// something more specific.
				t_pb* subtree_result =
					highlight_sub_block_helper(
						clb, child_pb, local_pt - bbox.bottom_left(), max_depth);
				if (subtree_result != NULL) {
					// we found something more specific.
					return subtree_result;
				} else {
					// couldn't find something more specific, return parent
					return child_pb;
				}
			}
		}
	}
	return NULL;
}

t_selected_sub_block_info& get_selected_sub_block_info() {
	// used to keep track of the selected sub-block.
	static t_selected_sub_block_info selected_sub_block_info;

	return selected_sub_block_info;
}

/*
 * Begin definition of t_selected_sub_block_info functions.
 */

t_selected_sub_block_info::t_selected_sub_block_info() {
	clear();
	clear_critical_path();
}

template<typename HashType>
void add_all_children(const t_pb* pb, const t_block* clb,
	std::unordered_set< t_selected_sub_block_info::gnode_clb_pair, HashType>& set) {

	if (pb == NULL) {
		return;
	}

	set.insert(t_selected_sub_block_info::gnode_clb_pair(pb->pb_graph_node, clb));

	int num_child_types = pb->get_num_child_types();
	for (int i = 0; i < num_child_types; ++i) {
		int num_children_of_type = pb->get_num_children_of_type(i);
		for (int j = 0; j < num_children_of_type; ++j) {
			add_all_children(&pb->child_pbs[i][j], clb, set);
		}
	}
}

void t_selected_sub_block_info::set(t_pb* new_selected_sub_block, t_block* new_containing_block) {
	selected_pb = new_selected_sub_block;
    selected_pb_gnode = (selected_pb == NULL) ? NULL : selected_pb->pb_graph_node;
	containing_block = new_containing_block;
	sinks.clear();
	sources.clear();
	in_selected_subtree.clear();

	// vtr::printf_info("selecting in clb #%ld\n", new_containing_block - block);
	if (has_selection()) {
		add_all_children(selected_pb, containing_block, in_selected_subtree);

		for (auto blk_id : g_atom_nl.blocks()) {
            const t_block* clb = &block[g_atom_lookup.atom_clb(blk_id)];
            const t_pb_graph_node* pb_graph_node = g_atom_lookup.atom_pb_graph_node(blk_id);
			// find the atom block that corrisponds to this pb.
			if ( is_in_selected_subtree(pb_graph_node, clb) ) {

                //Collect the sources of all nets driving this node
                for(auto pin_id : g_atom_nl.block_input_pins(blk_id)) {
                    AtomNetId net_id = g_atom_nl.pin_net(pin_id);
                    AtomPinId driver_pin_id = g_atom_nl.net_driver(net_id);

                    AtomBlockId src_blk = g_atom_nl.pin_block(driver_pin_id);

                    const t_block* src_clb = &block[g_atom_lookup.atom_clb(src_blk)];
                    const t_pb_graph_node* src_pb_graph_node = g_atom_lookup.atom_pb_graph_node(src_blk);

                    sources.insert(gnode_clb_pair(src_pb_graph_node, src_clb));
                }

                //Collect the sinks of all nets driven by this node
                for(auto pin_id : g_atom_nl.block_output_pins(blk_id)) {
                    AtomNetId net_id = g_atom_nl.pin_net(pin_id);
                    for(auto sink_pin_id : g_atom_nl.net_sinks(net_id)) {
                        AtomBlockId sink_blk = g_atom_nl.pin_block(sink_pin_id);

                        const t_block* sink_clb = &block[g_atom_lookup.atom_clb(sink_blk)];
                        const t_pb_graph_node* sink_pb_graph_node = g_atom_lookup.atom_pb_graph_node(sink_blk);

                        sinks.insert(gnode_clb_pair(sink_pb_graph_node, sink_clb));
                    }
                }
			}
		}
	}
}

void t_selected_sub_block_info::add_to_critical_path(const t_tnode& src_tnode, const t_tnode& sink_tnode) {

	driver_of_head_of_critical_path = gnode_clb_pair(
		src_tnode.pb_graph_pin->parent_node,
		&block[src_tnode.block]
	);
	blocks_on_critical_path.insert(driver_of_head_of_critical_path);

	head_of_critical_path = gnode_clb_pair(
		sink_tnode.pb_graph_pin->parent_node,
		&block[sink_tnode.block]
	);
	blocks_on_critical_path.insert(head_of_critical_path);

	nets_on_critical_path.insert(std::make_pair(
		clb_pin_tuple(
			 src_tnode.block,
			 src_tnode.pb_graph_pin->parent_node
		),
		clb_pin_tuple(
			sink_tnode.block,
			sink_tnode.pb_graph_pin->parent_node
		)
	));
}

void t_selected_sub_block_info::clear() {
	set(NULL, NULL);
}

void t_selected_sub_block_info::clear_critical_path() {
	nets_on_critical_path.clear();
	blocks_on_critical_path.clear();
	head_of_critical_path = gnode_clb_pair(NULL,NULL);
	driver_of_head_of_critical_path = gnode_clb_pair(NULL,NULL);
}

t_pb* t_selected_sub_block_info::get_selected_pb() const { return selected_pb; }

t_pb_graph_node* t_selected_sub_block_info::get_selected_pb_gnode() const { return selected_pb_gnode; }

t_block* t_selected_sub_block_info::get_containing_block() const { return containing_block; }

bool t_selected_sub_block_info::has_selection() const {
	return get_selected_pb_gnode() != NULL && get_containing_block() != NULL; 
}

bool t_selected_sub_block_info::is_selected(const t_pb_graph_node* test, const t_block* test_block) const {
	return get_selected_pb_gnode() == test && get_containing_block() == test_block;
}

bool t_selected_sub_block_info::is_sink_of_selected(const t_pb_graph_node* test, const t_block* test_block) const {
	return sinks.find(gnode_clb_pair(test,test_block)) != sinks.end();
}

bool t_selected_sub_block_info::is_source_of_selected(const t_pb_graph_node* test, const t_block* test_block) const {
	return sources.find(gnode_clb_pair(test,test_block)) != sources.end();
}

bool t_selected_sub_block_info::is_on_critical_path(const AtomPinId test_src, const AtomPinId test_sink) const {

	clb_pin_tuple src_clb_pin_tuple(test_src);
	clb_pin_tuple sink_clb_pin_tuple(test_sink);
	return nets_on_critical_path.find(
		std::make_pair(
			src_clb_pin_tuple,
			sink_clb_pin_tuple
		)
	) != nets_on_critical_path.end();
}

bool t_selected_sub_block_info::is_on_critical_path(const t_pb_graph_node* test, const t_block* test_block) const {
	return blocks_on_critical_path.find(gnode_clb_pair(test,test_block)) != blocks_on_critical_path.end();
}

bool t_selected_sub_block_info::is_head_net_of_critical_path(const AtomPinId test_src, const AtomPinId test_sink) const {
	clb_pin_tuple src_clb_pin_tuple(test_src);
	clb_pin_tuple sink_clb_pin_tuple(test_sink);

	return 
	is_driver_of_head_of_critical_path( src_clb_pin_tuple.pb_gnode, &block[ src_clb_pin_tuple.clb_index])
	&&        is_head_of_critical_path(sink_clb_pin_tuple.pb_gnode, &block[sink_clb_pin_tuple.clb_index]);
}

bool t_selected_sub_block_info::is_head_of_critical_path(const t_pb_graph_node* test, const t_block* test_block) const {
	return head_of_critical_path == gnode_clb_pair(test, test_block);
}

bool t_selected_sub_block_info::is_driver_of_head_of_critical_path(const t_pb_graph_node* test, const t_block* test_block) const {
	return driver_of_head_of_critical_path == gnode_clb_pair(test, test_block);
}

bool t_selected_sub_block_info::is_in_selected_subtree(const t_pb_graph_node* test, const t_block* test_block) const {
	return in_selected_subtree.find(gnode_clb_pair(test,test_block)) != in_selected_subtree.end();
}

/*
 * Begin definition of t_selected_sub_block_info::clb_pin_tuple functions.
 */

t_selected_sub_block_info::clb_pin_tuple::clb_pin_tuple(int clb_index_, const t_pb_graph_node* pb_gnode_) :
	clb_index(clb_index_),
	pb_gnode(pb_gnode_) {
}

t_selected_sub_block_info::clb_pin_tuple::clb_pin_tuple(const AtomPinId atom_pin) :
	clb_index(g_atom_lookup.atom_clb(g_atom_nl.pin_block(atom_pin))),
	pb_gnode(g_atom_lookup.atom_pb_graph_node(g_atom_nl.pin_block(atom_pin))) {
}

bool t_selected_sub_block_info::clb_pin_tuple::operator==(const clb_pin_tuple& rhs) const {
	return   clb_index == rhs.clb_index
	&&        pb_gnode == rhs.pb_gnode;
}

/*
 * Begin definition of t_selected_sub_block_info::gnode_clb_pair functions
 */

t_selected_sub_block_info::gnode_clb_pair::gnode_clb_pair() :
	pb_gnode(NULL),
	clb(NULL) {
}

t_selected_sub_block_info::gnode_clb_pair::gnode_clb_pair(const t_pb_graph_node* pb_gnode_, const t_block* clb_) :
	pb_gnode(pb_gnode_),
	clb(clb_) {
}

bool t_selected_sub_block_info::gnode_clb_pair::operator==(const gnode_clb_pair& rhs) const {
	return   clb == rhs.clb
	&&  pb_gnode == rhs.pb_gnode;
}
