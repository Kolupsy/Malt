#ifndef BLUR_GLSL
#define BLUR_GLSL

/*  META
    @uv: default=screen_uv();
    @radius: default=5.0;
*/
vec4 box_blur(sampler2D input_texture, vec2 uv, float radius, bool circular)
{
    vec2 resolution = textureSize(input_texture, 0);

    vec4 result = vec4(0);
    float total_weight = 0.0;

    for(float x = -radius; x <= radius; x++)
    {
        for(float y = -radius; y <= radius; y++)
        {
            vec2 offset = vec2(x,y);
            if(!circular || length(offset) <= radius)
            {
                result += texture(input_texture, uv + offset / resolution);
                total_weight += 1.0;
            }
        }   
    }

    return result / total_weight;
}

float _gaussian_weight(float x, float sigma)
{
    float sigma2 = sigma * sigma;

    return (1.0 / sqrt(2*PI*sigma2)) * exp(-(x*x, 2.0*sigma2));
}

float _gaussian_weight_2d(vec2 v, float sigma)
{
    float sigma2 = sigma * sigma;

    return (1.0 / (2*PI*sigma2)) * exp(-(dot(v,v) / (2.0*sigma2)));
}

/*  META
    @uv: default=screen_uv();
    @radius: default=5.0;
    @sigma: default=1.0;
*/
vec4 gaussian_blur(sampler2D input_texture, vec2 uv, float radius, float sigma)
{
    vec2 resolution = textureSize(input_texture, 0);

    vec4 result = vec4(0);
    float total_weight = 0.0;

    for(float x = -radius; x <= radius; x++)
    {
        for(float y = -radius; y <= radius; y++)
        {
            vec2 offset = vec2(x,y);
            float weight = _gaussian_weight_2d(offset, sigma);
            result += texture(input_texture, uv + offset / resolution) * weight;
            total_weight += weight;
        }   
    }

    return result / total_weight;
}

#include "Common/Math.glsl"

/*  META
    @uv: default=screen_uv();
    @radius: default=5.0;
    @distribution_exponent: default=5.0;
    @samples: default=8;
*/
vec4 jitter_blur(sampler2D input_texture, vec2 uv, float radius, float distribution_exponent, int samples)
{
    vec2 resolution = textureSize(input_texture, 0);
    vec4 result = vec4(0);

    for(int i = 0; i < samples;  i++)
    {
        vec4 random = random_per_pixel(i);
        float angle = random.x * PI * 2;
        float length = random.y;
        length = pow(length, distribution_exponent) * radius;
        float x = cos(angle) * length;
        float y = sin(angle) * length;
        vec2 offset = vec2(x,y) / resolution;
        result += texture(input_texture, uv + offset) / samples;
    }

    return result;
}

#endif //BLUR_GLSL
