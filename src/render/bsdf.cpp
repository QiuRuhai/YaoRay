#include <yaoray/render/bsdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float DeltaRoughness = 1.0e-3f;
constexpr float PlasticMinRoughness = 0.05f;

bool IsAboveSurface(Vec3f direction, Vec3f normal) {
    return Dot(direction, normal) > 0.0f;
}

Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}

float AbsDot(Vec3f a, Vec3f b) {
    return std::fabs(Dot(a, b));
}

float Clamp(float value, float low, float high) {
    return std::clamp(value, low, high);
}

float FresnelDielectric(float cos_theta_i, float eta_i, float eta_t) {
    cos_theta_i = Clamp(cos_theta_i, -1.0f, 1.0f);
    bool entering = cos_theta_i > 0.0f;
    if (!entering) {
        std::swap(eta_i, eta_t);
        cos_theta_i = std::fabs(cos_theta_i);
    }

    const float sin_theta_i = std::sqrt(std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    const float sin_theta_t = eta_i / eta_t * sin_theta_i;
    if (sin_theta_t >= 1.0f) {
        return 1.0f;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    const float r_parallel =
        ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) /
        ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
    const float r_perpendicular =
        ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) /
        ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));
    return 0.5f * (r_parallel * r_parallel + r_perpendicular * r_perpendicular);
}

struct DielectricFrame {
    Vec3f normal;
    float eta_i = 1.0f;
    float eta_t = 1.0f;
    float eta = 1.0f;
    float cos_o = 0.0f;
};

DielectricFrame MakeDielectricFrame(Vec3f wo, Vec3f normal, float ior) {
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    return DielectricFrame{
        oriented_normal,
        eta_i,
        eta_t,
        eta_i / eta_t,
        std::max(0.0f, Dot(oriented_normal, wo))
    };
}

bool Refract(Vec3f wo, Vec3f normal, float eta, Vec3f& wi) {
    const float cos_theta_i = Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) {
        return false;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
}

Vec3f SampleCosineHemisphere(Vec3f normal, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 1.0f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float radius = std::sqrt(u1);
    const float theta = 2.0f * Pi * u2;
    const float local_x = radius * std::cos(theta);
    const float local_y = radius * std::sin(theta);
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - u1));

    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

Color3f LambertianBrdf(Color3f albedo) {
    return albedo / Pi;
}

bool IsBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Color3f Lerp(Color3f a, Color3f b, float t) {
    return a * (1.0f - t) + b * t;
}

float RoughnessToAlpha(float roughness) {
    const float clamped = std::max(DeltaRoughness, roughness);
    return clamped * clamped;
}

float GgxDistribution(float cos_theta_h, float alpha) {
    const float cos2 = cos_theta_h * cos_theta_h;
    const float alpha2 = alpha * alpha;
    const float denom = cos2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (Pi * denom * denom);
}

float SmithG1(float cos_theta, float alpha) {
    const float cos2 = cos_theta * cos_theta;
    if (cos2 <= 0.0f) {
        return 0.0f;
    }
    const float tan2 = (1.0f - cos2) / cos2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alpha * alpha * tan2));
}

Color3f SchlickFresnel(Color3f f0, float cos_theta) {
    const float t = std::pow(1.0f - Clamp01(cos_theta), 5.0f);
    return Lerp(f0, Color3f{1.0f, 1.0f, 1.0f}, t);
}

Color3f GgxSpecularBrdf(Color3f f0, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return Color3f{};
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return Color3f{};
    }

    const float cos_o = std::max(0.0f, Dot(normal, wo));
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_oh <= 0.0f) {
        return Color3f{};
    }

    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const Color3f f = SchlickFresnel(f0, cos_oh);
    return f * (d * g / (4.0f * cos_o * cos_i));
}

float GgxReflectionPdf(float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return 0.0f;
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return 0.0f;
    }

    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_h <= 0.0f || cos_oh <= 0.0f) {
        return 0.0f;
    }

    const float alpha = RoughnessToAlpha(roughness);
    return GgxDistribution(cos_h, alpha) * cos_h / (4.0f * cos_oh);
}

Vec3f SampleGgxHalfVector(Vec3f normal, float roughness, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 0.999999f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float alpha = RoughnessToAlpha(roughness);
    const float alpha2 = alpha * alpha;
    const float tan2_theta = alpha2 * u1 / std::max(1.0e-6f, 1.0f - u1);
    const float cos_theta = 1.0f / std::sqrt(1.0f + tan2_theta);
    const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    const float phi = 2.0f * Pi * u2;

    const float local_x = sin_theta * std::cos(phi);
    const float local_y = sin_theta * std::sin(phi);
    const float local_z = cos_theta;
    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

BsdfSample SampleGgxReflection(Vec3f wo, Vec3f normal, Vec2f sample, Color3f f0, float roughness) {
    if (!IsAboveSurface(wo, normal) || IsBlack(f0)) {
        return BsdfSample{};
    }

    const Vec3f half_vector = SampleGgxHalfVector(normal, roughness, sample);
    const Vec3f wi = Reflect(-wo, half_vector);
    if (!IsAboveSurface(wi, normal)) {
        return BsdfSample{};
    }

    const float pdf = GgxReflectionPdf(roughness, wo, wi, normal);
    if (pdf <= 0.0f) {
        return BsdfSample{};
    }

    const Color3f f = GgxSpecularBrdf(f0, roughness, wo, wi, normal);
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    return BsdfSample{
        wi,
        f * (cos_i / pdf),
        pdf,
        true,
        false
    };
}

} // namespace

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            return LambertianBrdf(material.albedo);
        case MaterialKind::Mirror:
            return Color3f{};
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                return Color3f{};
            }
            return GgxSpecularBrdf(material.albedo, material.roughness, wo, wi, normal);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            const float specular = Clamp01(material.specular);
            const Color3f diffuse = LambertianBrdf(material.albedo) * (1.0f - specular);
            const Color3f f0{specular, specular, specular};
            const Color3f glossy = GgxSpecularBrdf(f0, std::max(material.roughness, PlasticMinRoughness), wo, wi, normal);
            return diffuse + glossy;
        }
        case MaterialKind::Dielectric:
            return Color3f{};
    }
    return Color3f{};
}

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case MaterialKind::Mirror:
            return 0.0f;
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                return 0.0f;
            }
            return GgxReflectionPdf(material.roughness, wo, wi, normal);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            const float specular = Clamp01(material.specular);
            const float diffuse_pdf = std::max(0.0f, Dot(normal, wi)) / Pi;
            if (specular <= 0.0f) {
                return diffuse_pdf;
            }
            const float glossy_pdf = GgxReflectionPdf(std::max(material.roughness, PlasticMinRoughness), wo, wi, normal);
            return 0.5f * diffuse_pdf + 0.5f * glossy_pdf;
        }
        case MaterialKind::Dielectric:
            return 0.0f;
    }
    return 0.0f;
}

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample) {
    switch (material.type) {
        case MaterialKind::Diffuse: {
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            return BsdfSample{
                wi,
                material.albedo,
                PdfBsdf(material, wo, wi, normal),
                true,
                false
            };
        }
        case MaterialKind::Mirror:
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            return BsdfSample{
                Reflect(-wo, normal),
                material.albedo,
                1.0f,
                true,
                true
            };
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                    return BsdfSample{};
                }
                return BsdfSample{
                    Reflect(-wo, normal),
                    material.albedo,
                    1.0f,
                    true,
                    true
                };
            }
            return SampleGgxReflection(wo, normal, sample, material.albedo, material.roughness);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal)) {
                return BsdfSample{};
            }
            const float specular = Clamp01(material.specular);
            Vec2f remapped = sample;
            if (specular <= 0.0f || sample.x < 0.5f) {
                remapped.x = specular <= 0.0f ? sample.x : sample.x * 2.0f;
                const Vec3f wi = SampleCosineHemisphere(normal, remapped);
                const float pdf = PdfBsdf(material, wo, wi, normal);
                if (pdf <= 0.0f) {
                    return BsdfSample{};
                }
                const Color3f f = EvaluateBsdf(material, wo, wi, normal);
                const float cos_i = std::max(0.0f, Dot(normal, wi));
                return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
            }
            remapped.x = (sample.x - 0.5f) * 2.0f;
            const Color3f f0{specular, specular, specular};
            BsdfSample result = SampleGgxReflection(
                wo,
                normal,
                remapped,
                f0,
                std::max(material.roughness, PlasticMinRoughness)
            );
            if (!result.valid) {
                return result;
            }
            const float pdf = PdfBsdf(material, wo, result.wi, normal);
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            const Color3f f = EvaluateBsdf(material, wo, result.wi, normal);
            const float cos_i = std::max(0.0f, Dot(normal, result.wi));
            result.weight = f * (cos_i / pdf);
            result.pdf = pdf;
            result.specular = false;
            return result;
        }
        case MaterialKind::Dielectric: {
            if (IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            if (material.thin) {
                const float fresnel = FresnelDielectric(std::fabs(Dot(normal, wo)), 1.0f, std::max(1.0f, material.ior));
                if (sample.x < fresnel) {
                    return BsdfSample{
                        Reflect(-wo, Dot(normal, wo) >= 0.0f ? normal : -normal),
                        material.albedo,
                        1.0f,
                        true,
                        true
                    };
                }
                return BsdfSample{Normalize(-wo), material.albedo, 1.0f, true, true};
            }
            if (material.roughness <= DeltaRoughness) {
                const DielectricFrame frame = MakeDielectricFrame(wo, normal, material.ior);
                Vec3f refracted;
                const bool can_refract = Refract(wo, frame.normal, frame.eta, refracted);
                const float fresnel = can_refract ? FresnelDielectric(frame.cos_o, frame.eta_i, frame.eta_t) : 1.0f;
                if (!can_refract || sample.x < fresnel) {
                    return BsdfSample{Reflect(-wo, frame.normal), material.albedo, 1.0f, true, true};
                }
                return BsdfSample{refracted, material.albedo, 1.0f, true, true};
            }
            return BsdfSample{};
        }
    }
    return BsdfSample{};
}

bool IsDeltaBsdf(const RenderMaterial& material) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            return false;
        case MaterialKind::Mirror:
            return true;
        case MaterialKind::Metal:
            return material.roughness <= DeltaRoughness;
        case MaterialKind::Plastic:
            return false;
        case MaterialKind::Dielectric:
            return material.roughness <= DeltaRoughness;
    }
    return false;
}

} // namespace yr
