// Emissive surface. BaseColor stays dark; the material color is emitted.
MaterialAttributes EvaluateMaterial(VSOutput input)
{
    MaterialAttributes attr = (MaterialAttributes)0;
    float3 tint = input.Color.rgb;
    if (input.HasBaseColorTex > 0.5)
        tint *= g_BaseColorTex.Sample(g_LinearWrap, input.TexCoord).rgb;

    float3 normal = input.NormalWS;
    if (dot(normal, normal) < 1e-8)
        normal = float3(0.0, 0.0, 1.0);

    attr.BaseColor = tint * 0.05;
    attr.Metallic = 0.0;
    attr.Specular = 0.5;
    attr.Roughness = 1.0;
    attr.Normal = normalize(normal);
    attr.AO = 1.0;
    attr.Opacity = input.Color.a;
    attr.Emissive = tint + input.Emissive;
    return attr;
}
