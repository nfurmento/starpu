#include <stdlib.h>
#include <ming.h>
#include <math.h>

#include "fxt-tool.h"

#define WIDTH	800
#define HEIGHT	600

#define THICKNESS	50
#define GAP		10

#define BORDERX		100
#define BORDERY		100

SWFMovie movie;

uint64_t absolute_start_time;
uint64_t start_time;
uint64_t absolute_end_time;
uint64_t end_time;

void flash_engine_init(void)
{
	Ming_init();

	Ming_setScale(1.0);

	movie = newSWFMovie();

	SWFMovie_setBackground(movie, 0xff, 0xff, 0xff);
	SWFMovie_setDimension(movie, WIDTH, HEIGHT);

}

#define PEN_WIDTH	0

void add_region(worker_mode color, uint64_t start, uint64_t end, unsigned worker)
{
	unsigned starty, endy, startx, endx;

	starty = BORDERY + (THICKNESS + GAP)*worker;
	endy = starty + THICKNESS;

	double ratio_start, ratio_end;
	
	ratio_start = (double)(start - start_time) / (double)(end_time - start_time);
	ratio_end = (double)(end - start_time) / (double)(end_time - start_time);

	startx = (unsigned)(BORDERX + ratio_start*(WIDTH - 2*BORDERX)); 
	endx = (unsigned)(BORDERX + ratio_end*(WIDTH - 2*BORDERX)); 

//	printf("startx %d endx %d  ratio %f %f starty %d endy %d\n", startx, endx, ratio_start, ratio_end, starty, endy);

	int region_color[3];
		switch (color) {
			case WORKING:
				region_color[0] = 0;
				region_color[1] = 255;
				region_color[2] = 0;
				break;
			case IDLE:
			default:
				region_color[0] = 255;
				region_color[1] = 0;
				region_color[2] = 0;
				break;
		}


	SWFShape shape = newSWFShape();
//	SWFShape_setLine(shape, PEN_WIDTH, region_color[0], region_color[1], region_color[2], 255);
	SWFShape_setLine(shape, PEN_WIDTH, 0, 0, 0, 255);

	SWFFillStyle style= SWFShape_addSolidFillStyle(shape, region_color[0], region_color[1], region_color[2], 255);
	SWFShape_setRightFillStyle(shape, style);


	SWFShape_movePenTo(shape, startx, starty);
	SWFShape_drawLine(shape, endx-startx, 0);
	SWFShape_drawLine(shape, 0, endy-starty);
	SWFShape_drawLine(shape, (int)startx-(int)endx, 0);
	SWFShape_drawLine(shape, 0, -((int)endy-(int)starty));
	
	SWFMovie_add(movie, (SWFBlock)shape);
}

void display_worker(event_list_t *events, unsigned worker)
{
	uint64_t prev = start_time;
	worker_mode prev_state = IDLE;
	worker_mode working_state = 0;

	event_itor_t i;
	for (i = event_list_begin(events);
		i != event_list_end(events);
		i = event_list_next(i))
	{
		add_region(prev_state, prev, i->time, worker);

		prev = i->time;
		prev_state = i->mode;
	}
}

char str_start[20];
char str_end[20];

void display_start_end_buttons()
{
	SWFFont font;
	unsigned x_start, x_end, y;
	unsigned size = 15;

	sprintf(str_start, "start\n%lu", start_time-absolute_start_time);
	sprintf(str_end, "end\n%lu", end_time -absolute_start_time);

	const char *fontpath = "Sans.fdb";
	FILE *f = fopen(fontpath,"r");
	ASSERT(f);

	x_start = BORDERX;
	x_end = WIDTH - BORDERX;
	y = BORDERY/2;

	font = loadSWFFontFromFile(f);
	if (font == NULL) {
		perror("could not open font :");
		exit(-1);
	}

	SWFText text_start = newSWFText();
	SWFText_setFont(text_start, font);
	SWFText_setColor(text_start, 0, 0, 0, 0xff);
	SWFText_setHeight(text_start, size);
	SWFText_moveTo(text_start, x_start, y);
	SWFText_addString(text_start, str_start, NULL);

	SWFText text_end = newSWFText();
	SWFText_setFont(text_end, font);
	SWFText_setColor(text_end, 0, 0, 0, 0xff);
	SWFText_setHeight(text_end, size);
	SWFText_moveTo(text_end, x_end, y);
	SWFText_addString(text_end, str_end, NULL);

	SWFMovie_add(movie, text_start);
	SWFMovie_add(movie, text_end);


}

void flash_engine_generate_output(event_list_t **events, unsigned nworkers, 
				uint64_t _start_time, uint64_t _end_time, char *path)
{
	unsigned worker;

	start_time = _start_time;
	absolute_start_time = _start_time;
	end_time = _end_time;
	absolute_end_time = _end_time;

	display_start_end_buttons();

	for (worker = 0; worker < nworkers; worker++)
	{
		display_worker(events[worker], worker);
	}

	printf("save output ... \n");

	SWFMovie_save(movie, path);
}
