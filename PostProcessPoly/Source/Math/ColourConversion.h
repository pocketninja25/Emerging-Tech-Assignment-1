#pragma once

void HSLToRGB(float H, float S, float L, float& R, float& G, float& B);

void HSLToRGB(int H, int S, int L, float& R, float& G, float& B);

void RGBToHSL(float R, float G, float B, int& H, int& S, int& L);
