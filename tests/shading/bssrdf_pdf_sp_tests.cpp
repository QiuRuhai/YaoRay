#include "yr_test.hpp"
#include <yaoray/shading/bssrdf.hpp>
#include <cmath>

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

YR_TEST(bssrdf_pdf_sp_basic) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::Point3f pi{0.05f, 0.0f, 0.0f};
    yr::Vec3f ni{0, 0, 1};
    float pdf = s.Pdf_Sp(po, ss, ts, ns, pi, ni);
    YR_EXPECT_TRUE(std::isfinite(pdf) && pdf > 0.0f);
}

YR_TEST(bssrdf_pdf_sp_decays_with_distance) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1}, ni{0, 0, 1};
    float near_pdf = s.Pdf_Sp(po, ss, ts, ns, yr::Point3f{0.01f, 0, 0}, ni);
    float far_pdf = s.Pdf_Sp(po, ss, ts, ns, yr::Point3f{0.3f, 0, 0}, ni);
    YR_EXPECT_TRUE(near_pdf > far_pdf);
}

YR_TEST(bssrdf_pdf_sp_finite_for_grazing_normal) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::Point3f pi{0.05f, 0.05f, 0.0f};
    yr::Vec3f ni{1, 0, 0};
    float pdf = s.Pdf_Sp(po, ss, ts, ns, pi, ni);
    YR_EXPECT_TRUE(std::isfinite(pdf) && pdf >= 0.0f);
}
