#include "ColourConversion.h"

// Find the minimum of three numbers (helper function for exercise below)
float Min(float f1, float f2, float f3)
{
	float fMin = f1;
	if (f2 < fMin)
	{
		fMin = f2;
	}
	if (f3 < fMin)
	{
		fMin = f3;
	}
	return fMin;
}

// Find the maximum of three numbers (helper function for exercise below)
float Max(float f1, float f2, float f3)
{
	float fMax = f1;
	if (f2 > fMax)
	{
		fMax = f2;
	}
	if (f3 > fMax)
	{
		fMax = f3;
	}
	return fMax;
}

// Convert an RGB colour to a HSL colour
void RGBToHSL(float R, float G, float B, int& H, int& S, int& L)
{
	// Fill in the correct code here for question 4, the functions Min and Max above will help

	float fR = R;
	float fG = G;
	float fB = B;

	float MIN = Min(fR, fG, fB);
	float MAX = Max(fR, fG, fB);

	L = 50 * (MAX + MIN);

	if (MIN == MAX)
	{
		S = H = 0;
		return;
	}

	if (L < 50)
	{
		S = 100 * (MAX - MIN) / (MAX + MIN);
	}
	else
	{
		S = 100 * (MAX - MIN) / (2.0f - MAX - MIN);
	}

	if (MAX == fR)
	{
		H = 60 * (fG - fB) / (MAX - MIN);
	}
	if (MAX == fG)
	{
		H = 60 * (fB - fR) / (MAX - MIN) + 120;
	}
	if (MAX == fB)
	{
		H = 60 * (fR - fG) / (MAX - MIN) + 240;
	}

	if (H < 0)
	{
		H += 360;
	}
}


float HueToRGB(float t1, float t2, float tH)	//Function used in conversion of hsl to rgb 
//tH will be either of the temporary RGB values 
//Returns the 0.0 - 1.0 R, G or B value
{
	//The value of hue at this point could be between -1 and 2, the value needs to be between 0 and 1
	//To do this either add or subtract 1 accordingly (unless the number is already in the correct range
	if (tH < 0)
	{
		tH += 1;
	}
	if (tH > 1)
	{
		tH -= 1;
	}


	//Now perform the correct calculation to the hue depending on its value
	if ((6.0f * tH) < 1.0f)
	{
		return (t2 + (t1 - t2) * 6.0f * tH);
	}
	if ((2.0f * tH) < 1.0f)
	{
		return t1;
	}
	if ((3.0f * tH) < 2.0f)
	{
		return (t2 + (t1 - t2) * ((2.0f / 3.0f) - tH) * 6.0f);
	}
	return t2;

}

void HSLToRGB(float H, float S, float L, float& R, float& G, float& B)
{
	int h = H * 360;
	int s = S * 100;
	int l = L * 100;
	HSLToRGB(h, s, l, R, G, B);
}

// Convert a HSL colour to an RGB colour
void HSLToRGB(int H, int S, int L, float& R, float& G, float& B)
{
	//H: 0 - 360
	//S: 0 - 100
	//L: 0 - 100

	//Convert HSL values to decimals (0-1)
	int iR, iG, iB;

	float tH = H / 360.0f;	//Convert Hue (0-360 [degrees])
	float tS = S / 100.0f;	//Convert Saturation (0 - 100)
	float tL = L / 100.0f;	//Convert Luminance (0 - 100)

	if (tS == 0)	//If no saturation, then colour is a shade of grey
	{
		//Convert L (0-100) to RGB (0-255)
		iR = iG = iB = (tL * (255.0f / 100.0f));
	}
	else
	{
		float temp1, temp2;
		//Luminance less than 50%
		if (tL < 0.5f)
		{
			temp1 = tL * (tS + 1.0f);
		}
		else
		{
			temp1 = (tL + tS) - (tL * tS);
		}

		temp2 = 2 * tL - temp1;


		//Convert each temporary hue (RGB) to its final value
		R = HueToRGB(temp1, temp2, tH + (1.0f / 3.0f));
		G = HueToRGB(temp1, temp2, tH);
		B = HueToRGB(temp1, temp2, tH - (1.0f / 3.0f));

	}

}
