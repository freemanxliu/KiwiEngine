MaterialAttributes EvaluateMaterial(FSIn input, texture2d<float> baseColorTex, sampler linearSampler)
{
    MaterialAttributes attr;
    float3 baseColor = input.color.rgb;
    if (input.hasBase > 0.5)
        baseColor *= baseColorTex.sample(linearSampler, input.uv).rgb;

    float3 normal = input.normalWS;
    if (dot(normal, normal) < 1e-8)
        normal = float3(0.0, 0.0, 1.0);

    attr.baseColor = baseColor;
    attr.metallic = input.metallic;
    attr.specular = 0.5;
    attr.roughness = max(input.roughness, 0.04);
    attr.normal = normalize(normal);
    attr.ao = 1.0;
    attr.opacity = input.color.a;
    attr.emissive = input.emissive;
    if (input.shadingModel < 0.5)
        attr.emissive = baseColor;
    return attr;
}
