// Default surface. Writes BaseColor, Roughness, Metallic and Emissive.
// Emissive comes from the material _Emissive color. Unlit uses BaseColor as Emissive.
MaterialAttributes EvaluateMaterial(VSOutput input)
{
    MaterialAttributes attr = (MaterialAttributes)0;
    float3 baseColor = input.Color.rgb;
    if (input.HasBaseColorTex > 0.5)
        baseColor *= g_BaseColorTex.Sample(g_LinearWrap, input.TexCoord).rgb;

    float3 normal = input.NormalWS;
    if (dot(normal, normal) < 1e-8)
        normal = float3(0.0, 0.0, 1.0);

    attr.BaseColor = baseColor;
    attr.Metallic = input.Metallic;
    attr.Specular = 0.5;
    attr.Roughness = max(input.Roughness, 0.04);
    attr.Normal = normalize(normal);
    attr.AO = 1.0;
    attr.Opacity = input.Color.a;
    attr.Emissive = input.Emissive;
    if (input.ShadingModelID < 0.5)
        attr.Emissive = baseColor;
    return attr;
}
