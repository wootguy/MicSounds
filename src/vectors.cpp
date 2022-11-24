#include "vectors.h"
#include <cmath>

#define EPSILON 0.01f

bool operator==( vec3 v1, vec3 v2 )
{
	vec3 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return false;
	if (fabs(v.y) >= EPSILON)
		return false;
	if (fabs(v.z) >= EPSILON)
		return false;
	return true;
}

bool operator!=( vec3 v1, vec3 v2 )
{
	vec3 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return true;
	if (fabs(v.y) >= EPSILON)
		return true;
	if (fabs(v.z) >= EPSILON)
		return true;
	return false;
}

vec3 operator-( vec3 v1, vec3 v2 )
{
	v1.x -= v2.x;
	v1.y -= v2.y;
	v1.z -= v2.z;
	return v1;
}

vec3 operator+( vec3 v1, vec3 v2 )
{
	v1.x += v2.x;
	v1.y += v2.y;
	v1.z += v2.z;
	return v1;
}

vec3 operator*( vec3 v1, vec3 v2 )
{
	v1.x *= v2.x;
	v1.y *= v2.y;
	v1.z *= v2.z;
	return v1;
}

vec3 operator/( vec3 v1, vec3 v2 )
{
	v1.x /= v2.x;
	v1.y /= v2.y;
	v1.z /= v2.z;
	return v1;
}

vec3 operator-( vec3 v, float f )
{
	v.x -= f;
	v.y -= f;
	v.z -= f;
	return v;
}

vec3 operator+( vec3 v, float f )
{
	v.x += f;
	v.y += f;
	v.z += f;
	return v;
}

vec3 operator*( vec3 v, float f )
{
	v.x *= f;
	v.y *= f;
	v.z *= f;
	return v;
}

vec3 operator/( vec3 v, float f )
{
	v.x /= f;
	v.y /= f;
	v.z /= f;
	return v;
}

void vec3::operator-=( vec3 v )
{
	x -= v.x;
	y -= v.y;
	z -= v.z;
}

void vec3::operator+=( vec3 v )
{
	x += v.x;
	y += v.y;
	z += v.z;
}

void vec3::operator*=( vec3 v )
{
	x *= v.x;
	y *= v.y;
	z *= v.z;
}

void vec3::operator/=( vec3 v )
{
	x /= v.x;
	y /= v.y;
	z /= v.z;
}

void vec3::operator-=( float f )
{
	x -= f;
	y -= f;
	z -= f;
}

void vec3::operator+=( float f )
{
	x += f;
	y += f;
	z += f;
}

void vec3::operator*=( float f )
{
	x *= f;
	y *= f;
	z *= f;
}

void vec3::operator/=( float f )
{
	x /= f;
	y /= f;
	z /= f;
}

vec3 crossProduct( vec3 v1, vec3 v2 )
{
	float x = v1.y*v2.z - v2.y*v1.z; 
	float y = v2.x*v1.z - v1.x*v2.z; 
	float z = v1.x*v2.y - v1.y*v2.x;
	return vec3(x, y, z);
}

float dotProduct( vec3 v1, vec3 v2 )
{
	return v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}

vec3 vec3::normalize( float length )
{
	if (x == 0 && y == 0 && z == 0)
		return vec3(0, 0, 0);
	float d = length / sqrt( (x*x) + (y*y) + (z*z) );
	return vec3(x*d, y*d, z*d);
}

vec3 vec3::invert() {
	return vec3(x != 0 ? -x : x, y != 0 ? -y : y, z != 0 ? -z : z);
}

float vec3::length()
{
	return sqrt( (x*x) + (y*y) + (z*z) );
}

vec3 vec3::flip() {
	return vec3(x, z, -y);
}


bool operator==(vec2 v1, vec2 v2)
{
	vec2 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return false;
	if (fabs(v.y) >= EPSILON)
		return false;
	return true;
}

bool operator!=(vec2 v1, vec2 v2)
{
	return v1.x != v2.x || v1.y != v2.y;
}

vec2 operator-(vec2 v1, vec2 v2)
{
	v1.x -= v2.x;
	v1.y -= v2.y;
	return v1;
}

vec2 operator+(vec2 v1, vec2 v2)
{
	v1.x += v2.x;
	v1.y += v2.y;
	return v1;
}

vec2 operator*(vec2 v1, vec2 v2)
{
	v1.x *= v2.x;
	v1.y *= v2.y;
	return v1;
}

vec2 operator/(vec2 v1, vec2 v2)
{
	v1.x /= v2.x;
	v1.y /= v2.y;
	return v1;
}

vec2 operator-(vec2 v, float f)
{
	v.x -= f;
	v.y -= f;
	return v;
}

vec2 operator+(vec2 v, float f)
{
	v.x += f;
	v.y += f;
	return v;
}

vec2 operator*(vec2 v, float f)
{
	v.x *= f;
	v.y *= f;
	return v;
}

vec2 operator/(vec2 v, float f)
{
	v.x /= f;
	v.y /= f;
	return v;
}

void vec2::operator-=(vec2 v)
{
	x -= v.x;
	y -= v.y;
}

void vec2::operator+=(vec2 v)
{
	x += v.x;
	y += v.y;
}

void vec2::operator*=(vec2 v)
{
	x *= v.x;
	y *= v.y;
}

void vec2::operator/=(vec2 v)
{
	x /= v.x;
	y /= v.y;
}

void vec2::operator-=(float f)
{
	x -= f;
	y -= f;
}

void vec2::operator+=(float f)
{
	x += f;
	y += f;
}

void vec2::operator*=(float f)
{
	x *= f;
	y *= f;
}

void vec2::operator/=(float f)
{
	x /= f;
	y /= f;
}

float vec2::length()
{
	return sqrt((x * x) + (y * y));
}

vec2 vec2::normalize(float length) {
	if (x == 0 && y == 0)
		return vec2(0, 0);
	float d = length / sqrt((x * x) + (y * y));
	return vec2(x * d, y * d);
}



bool operator==(vec4 v1, vec4 v2)
{
	vec4 v = v1 - v2;
	if (fabs(v.x) >= EPSILON)
		return false;
	if (fabs(v.y) >= EPSILON)
		return false;
	if (fabs(v.z) >= EPSILON)
		return false;
	if (fabs(v.w) >= EPSILON)
		return false;
	return true;
}


bool operator!=(vec4 v1, vec4 v2)
{
	return v1.x != v2.x || v1.y != v2.y || v1.z != v2.z || v1.w != v2.w;
}


vec4 operator+(vec4 v1, vec4 v2)
{
	v1.x += v2.x;
	v1.y += v2.y;
	v1.z += v2.z;
	v1.w += v2.w;
	return v1;
}

vec4 operator+(vec4 v, float f)
{
	v.x += f;
	v.y += f;
	v.z += f;
	v.w += f;
	return v;
}



vec4 operator*(vec4 v1, vec4 v2)
{
	v1.x *= v2.x;
	v1.y *= v2.y;
	v1.z *= v2.z;
	v1.w *= v2.w;
	return v1;
}

vec4 operator*(vec4 v, float f)
{
	v.x *= f;
	v.y *= f;
	v.z *= f;
	v.w *= f;
	return v;
}



vec4 operator/(vec4 v1, vec4 v2)
{
	v1.x /= v2.x;
	v1.y /= v2.y;
	v1.z /= v2.z;
	v1.w /= v2.w;
	return v1;
}

vec4 operator/(vec4 v, float f)
{
	v.x /= f;
	v.y /= f;
	v.z /= f;
	v.w /= f;
	return v;
}


vec4 operator-(vec4 v1, vec4 v2)
{
	v1.x -= v2.x;
	v1.y -= v2.y;
	v1.z -= v2.z;
	v1.w -= v2.w;
	return v1;
}

vec4 operator-(vec4 v, float f)
{
	v.x -= f;
	v.y -= f;
	v.z -= f;
	v.w -= f;
	return v;
}

vec3 vec4::xyz() { 
	return vec3(x, y, z); 
}

vec2 vec4::xy() {
	return vec2(x, y);
}
