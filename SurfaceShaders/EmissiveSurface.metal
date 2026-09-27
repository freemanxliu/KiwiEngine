MaterialAttributes EvaluateMaterial(FSIn input, texture2d<float> baseColorTex, sampler linearSampler)
{
    MaterialAttributes attr;
    float3 tint = input.color.rgb;
    if (input.hasBase > 0.5)
        tint *= baseColorTex.sample(linearSampler, input.uv).rgb;

    float3 normal = input.normalWS;
    if (dot(normal, normal) < 1e-8)
        normal = float3(0.0, 0.0, 1.0);

    attr.baseColor = tint * 0.05;
    attr.metallic = 0.0;
    attr.specular = 0.5;
    attr.roughness = 1.0;
    attr.normal = normalize(normal);
    attr.ao = 1.0;
    attr.opacity = input.color.a;
    attr.emissive = tint + input.emissive;
    return attr;
}
