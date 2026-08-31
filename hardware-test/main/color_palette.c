#include "color_palette.h"

#include <stddef.h>

const color_palette_t COLOR_PALETTES[COLOR_PALETTE_COUNT] = {
    { "Northern Lights", {
        { 0, 195, 255 }, { 115, 230, 255 }, // ice blue + tint
        { 90, 55, 255 },  { 175, 145, 255 }, // violet + tint
        { 245, 35, 185 }, { 255, 145, 220 }, // magenta + tint
        { 0, 220, 145 },  { 120, 255, 205 }, // mint + tint
    } },
    { "Deep Ocean", {
        { 0, 35, 135 },   { 80, 125, 220 }, // navy + tint
        { 0, 100, 235 },  { 100, 175, 255 }, // blue + tint
        { 0, 205, 220 },  { 105, 245, 245 }, // aqua + tint
        { 0, 175, 120 },  { 110, 230, 190 }, // seafoam + tint
    } },
    { "Desert Sunset", {
        { 215, 20, 85 },  { 255, 125, 170 }, // raspberry + tint
        { 255, 70, 25 },  { 255, 155, 105 }, // coral + tint
        { 255, 145, 0 },  { 255, 205, 95 },  // amber + tint
        { 255, 215, 0 },  { 255, 240, 125 }, // gold + tint
    } },
    { "Tropical Lagoon", {
        { 0, 125, 145 },  { 85, 205, 215 }, // lagoon teal + tint
        { 0, 225, 205 },  { 105, 255, 235 }, // turquoise + tint
        { 50, 220, 70 },  { 145, 255, 150 }, // palm green + tint
        { 255, 180, 0 },  { 255, 220, 100 }, // mango + tint
    } },
    { "Forest Canopy", {
        { 0, 70, 55 },    { 75, 145, 125 }, // pine + tint
        { 0, 155, 80 },   { 95, 220, 145 }, // emerald + tint
        { 95, 145, 0 },   { 175, 210, 80 }, // moss + tint
        { 185, 225, 0 },  { 225, 250, 100 }, // new leaf + tint
    } },
    { "Molten Ember", {
        { 210, 0, 15 },   { 255, 105, 100 }, // scarlet + tint
        { 255, 45, 0 },   { 255, 135, 90 }, // vermilion + tint
        { 255, 110, 0 },  { 255, 185, 80 }, // orange + tint
        { 255, 190, 0 },  { 255, 230, 105 }, // flame gold + tint
    } },
    { "Lavender Dawn", {
        { 35, 45, 175 },  { 115, 135, 235 }, // dawn blue + tint
        { 115, 70, 220 }, { 185, 145, 250 }, // lavender + tint
        { 215, 75, 205 }, { 250, 165, 235 }, // orchid + tint
        { 255, 140, 105 }, { 255, 205, 170 }, // peach + tint
    } },
    { "Neon Arcade", {
        { 0, 85, 255 },   { 100, 165, 255 }, // electric blue + tint
        { 155, 20, 255 }, { 210, 125, 255 }, // ultraviolet + tint
        { 255, 0, 135 },  { 255, 120, 195 }, // hot pink + tint
        { 165, 255, 0 },  { 215, 255, 110 }, // neon lime + tint
    } },
};

void color_palette_sample(uint8_t palette_index, uint8_t position, uint8_t brightness,
                          uint8_t *red, uint8_t *green, uint8_t *blue)
{
    const color_palette_t *palette = &COLOR_PALETTES[palette_index % COLOR_PALETTE_COUNT];
    uint16_t scaled_position = position * COLOR_PALETTE_COLOUR_COUNT;
    uint8_t colour_index = scaled_position >> 8;
    uint8_t mix = scaled_position & 0xff;
    uint8_t next_index = (colour_index + 1) % COLOR_PALETTE_COLOUR_COUNT;
    uint8_t mixed[3];

    for (size_t channel = 0; channel < 3; ++channel) {
        mixed[channel] = ((uint16_t)palette->colour[colour_index][channel] * (255 - mix)
                        + (uint16_t)palette->colour[next_index][channel] * mix) / 255;
    }
    *red = ((uint16_t)mixed[0] * brightness) / 255;
    *green = ((uint16_t)mixed[1] * brightness) / 255;
    *blue = ((uint16_t)mixed[2] * brightness) / 255;
}
