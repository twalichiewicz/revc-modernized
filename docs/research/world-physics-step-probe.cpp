// Whole-step conservation regression for the research world only.
// Reuse the original fixture exactly; its main is retained as an optional baseline.
#define main InitialWorldProbeMain
#include "world-physics-probe.cpp"
#undef main
#include <limits>

struct State {
    btVector3 p{0, 0, 0}, l{0, 0, 0};
    btScalar mass = 0, kinetic = 0, strain = 0;
};

State MeasureWorld(const World &w) {
    State s;
    for (const auto &body : w.rigid) {
        if (body->getInvMass() <= 0)
            continue;
        const btScalar mass = 1 / body->getInvMass();
        const auto p = body->getLinearVelocity() * mass;
        const auto basis = body->getWorldTransform().getBasis();
        const auto localOmega = basis.transpose() * body->getAngularVelocity();
        btVector3 localL(0, 0, 0);
        for (int i = 0; i < 3; ++i)
            if (body->getInvInertiaDiagLocal()[i] > 0)
                localL[i] = localOmega[i] / body->getInvInertiaDiagLocal()[i];
        s.mass += mass;
        s.p += p;
        s.l += body->getWorldTransform().getOrigin().cross(p) + basis * localL;
        s.kinetic += (body->getLinearVelocity().dot(p) + localOmega.dot(localL)) * btScalar(.5);
    }
    for (const auto &body : w.soft) {
        for (int i = 0; i < body->m_nodes.size(); ++i) {
            const auto &node = body->m_nodes[i];
            if (node.m_im <= 0)
                continue;
            const auto p = node.m_v / node.m_im;
            s.mass += 1 / node.m_im;
            s.p += p;
            s.l += node.m_x.cross(p);
            s.kinetic += node.m_v.dot(p) * btScalar(.5);
        }
        // Matches this fixture's authored 100 N/m axial and bending links.
        for (int i = 0; i < body->m_links.size(); ++i) {
            const auto &link = body->m_links[i];
            const auto extension = (link.m_n[1]->m_x - link.m_n[0]->m_x).length() - link.m_rl;
            s.strain += btScalar(50) * extension * extension;
        }
    }
    return s;
}

struct Trace {
    State initial;
    double p = 0, l = 0, gain = 0, mass = 0;
    double primaryBalance = 0, secondaryBalance = 0, maxRecipientImpulse = 0;
    World *fixture;
    std::vector<btVector3> beforeContact, afterContact;
    int callbacks = 0, samples = 0;
    bool pending = false, receiptFinite = true;
    static std::vector<btVector3> Momenta(const World &w) {
        std::vector<btVector3> values;
        for (const auto &body : w.rigid)
            values.push_back(body->getInvMass() > 0 ? body->getLinearVelocity() / body->getInvMass()
                                                    : btVector3(0, 0, 0));
        for (const auto &body : w.soft)
            for (int i = 0; i < body->m_nodes.size(); ++i) {
                const auto &n = body->m_nodes[i];
                values.push_back(n.m_im > 0 ? n.m_v / n.m_im : btVector3(0, 0, 0));
            }
        return values;
    }
    double Transfer(const std::vector<btVector3> &before, const std::vector<btVector3> &after) {
        if (before.size() != after.size()) {
            receiptFinite = false;
            return 0;
        }
        btVector3 net(0, 0, 0);
        for (size_t i = 0; i < before.size(); ++i) {
            const auto impulse = after[i] - before[i];
            receiptFinite &= std::isfinite(double(impulse.length()));
            maxRecipientImpulse = std::max(maxRecipientImpulse, double(impulse.length()));
            net += impulse;
        }
        return double(net.length());
    }
    static void ContactCallback(btScalar, btDeformableMultiBodyDynamicsWorld *world) {
        auto &self = *static_cast<Trace *>(world->getWorldUserInfo());
        ++self.callbacks;
        if (!self.pending) {
            self.beforeContact = Momenta(*self.fixture);
            self.pending = true;
        } else {
            self.afterContact = Momenta(*self.fixture);
            self.primaryBalance =
                std::max(self.primaryBalance, self.Transfer(self.beforeContact, self.afterContact));
            self.pending = false;
        }
    }
    explicit Trace(World &w) : initial(MeasureWorld(w)), fixture(&w) {
        w.world->setWorldUserInfo(this);
        w.world->setSolverCallback(ContactCallback);
    }
    ~Trace() {
        fixture->world->setSolverCallback(nullptr);
        fixture->world->setWorldUserInfo(nullptr);
    }
    Trace(const Trace &) = delete;
    Trace &operator=(const Trace &) = delete;
    bool Add(const World &w) {
        ++samples;
        if (pending || callbacks != samples * 2)
            return false;
        secondaryBalance = std::max(secondaryBalance, Transfer(afterContact, Momenta(w)));
        const auto s = MeasureWorld(w);
        const double dp = double((s.p - initial.p).length());
        const double dl = double((s.l - initial.l).length());
        const double de = double(s.kinetic + s.strain - initial.kinetic - initial.strain);
        const double dm = std::fabs(double(s.mass - initial.mass));
        if (!std::isfinite(dp + dl + de + dm))
            return false;
        p = std::max(p, dp);
        l = std::max(l, dl);
        gain = std::max(gain, de);
        mass = std::max(mass, dm);
        return true;
    }
    bool Conservative() const {
        return p < 1e-6 * (1 + double(initial.p.length())) &&
               l < .01 * (1 + double(initial.l.length())) &&
               gain < .01 * (1 + double(initial.kinetic + initial.strain)) && mass < 1e-9 &&
               receiptFinite && primaryBalance < 1e-6 * (1 + double(initial.p.length())) &&
               secondaryBalance < 1e-6 * (1 + double(initial.p.length()));
    }
};

void Nonuniform(btSoftBody &sheet, btScalar mass) {
    for (int i = 0; i < sheet.m_nodes.size(); ++i)
        sheet.setMass(i, (i % 3 == 0) ? .3 : (i % 3 == 1 ? 1 : 2));
    sheet.setTotalMass(mass);
}

btVector3 SegmentPoint(btVector3 a, btVector3 b, btVector3 point) {
    const auto edge = b - a;
    const auto length2 = edge.length2();
    if (length2 <= 1e-20)
        return a;
    return a + edge * btClamped((point - a).dot(edge) / length2, btScalar(0), btScalar(1));
}

// Signed clearance against current local triangles, rather than the sheet's
// mean height: a physical sheet can bow below its average and still block a ball.
double SphereClearance(const btSoftBody &sheet, btVector3 centre, btScalar radius,
                       btScalar winding) {
    btScalar nearest2 = BT_LARGE_FLOAT, signedDistance = BT_LARGE_FLOAT;
    bool found = false;
    for (int i = 0; i < sheet.m_faces.size(); ++i) {
        const auto &face = sheet.m_faces[i];
        const auto a = face.m_n[0]->m_x, b = face.m_n[1]->m_x, c = face.m_n[2]->m_x;
        const auto ab = b - a, ac = c - a, cross = ab.cross(ac);
        if (cross.length2() < 1e-20)
            continue;
        const auto normal = cross.normalized() * winding;
        auto closest = centre - normal * (centre - a).dot(normal);
        const btScalar aa = ab.dot(ab), bb = ab.dot(ac), cc = ac.dot(ac);
        const btScalar u =
            ((closest - a).dot(ab) * cc - (closest - a).dot(ac) * bb) / (aa * cc - bb * bb);
        const btScalar v =
            ((closest - a).dot(ac) * aa - (closest - a).dot(ab) * bb) / (aa * cc - bb * bb);
        if (u < 0 || v < 0 || u + v > 1) {
            closest = SegmentPoint(a, b, centre);
            for (auto alternative : {SegmentPoint(b, c, centre), SegmentPoint(c, a, centre)})
                if ((alternative - centre).length2() < (closest - centre).length2())
                    closest = alternative;
        }
        const auto delta = centre - closest;
        if (delta.length2() < nearest2) {
            found = true;
            nearest2 = delta.length2();
            signedDistance = btSqrt(nearest2) * (delta.dot(normal) < 0 ? -1 : 1);
        }
    }
    return found ? double(signedDistance - radius) : std::numeric_limits<double>::quiet_NaN();
}

bool RigidSheet(Settings settings, btScalar sheetMass, bool heterogeneous, bool reverse, bool miss,
                int location = 0) {
    World w(settings);
    const btVector3 hit = location ? btVector3(.41, .25, .6) : btVector3(.13, -.07, .6);
    btRigidBody *ball = nullptr;
    if (reverse)
        ball = w.Body(new btSphereShape(.2), 1, hit, {0, 0, -5});
    auto sheet = w.Sheet(0, sheetMass, 0);
    if (heterogeneous)
        Nonuniform(*sheet, sheetMass);
    if (!reverse)
        ball = w.Body(new btSphereShape(.2), 1, miss ? btVector3(3, 0, .6) : hit, {0, 0, -5});
    Trace trace(w);
    bool finite = true;
    int contacts = 0;
    const auto &first = sheet->m_faces[0];
    const btScalar winding =
        (first.m_n[1]->m_x - first.m_n[0]->m_x).cross(first.m_n[2]->m_x - first.m_n[0]->m_x).z() < 0
            ? -1
            : 1;
    double minimumClearance = 1e30;
    for (int i = 0; i < w.Steps(.5); ++i) {
        w.Step();
        contacts += sheet->m_faceRigidContacts.size() + sheet->m_nodeRigidContacts.size();
        finite &= trace.Add(w);
        const auto clearance =
            SphereClearance(*sheet, ball->getWorldTransform().getOrigin(), .2, winding);
        finite &= std::isfinite(clearance);
        minimumClearance = std::min(minimumClearance, clearance);
    }
    const bool interaction =
        miss ? std::fabs(double(ball->getLinearVelocity().z()) + 5) < 1e-8
             : contacts > 0 && ball->getLinearVelocity().z() > -4.9 && minimumClearance > -.02;
    const bool pass = finite && trace.Conservative() && interaction;
    std::printf("world rigid-sheet mass=%.3g nonuniform=%d reverse=%d miss=%d location=%d dt=%.9f "
                "projection=%d contacts=%d max_P=%.9g max_L=%.9g max_energy_gain=%.9g "
                "mass_error=%.3g min_clearance=%.6f primary_P=%.3g secondary_P=%.3g "
                "max_recipient_I=%.6g %s\n",
                double(sheetMass), heterogeneous, reverse, miss, location, double(settings.dt),
                settings.projection, contacts, trace.p, trace.l, trace.gain, trace.mass,
                minimumClearance, trace.primaryBalance, trace.secondaryBalance,
                trace.maxRecipientImpulse, pass ? "PASS" : "FAIL");
    return pass;
}

bool Sheets(Settings settings, bool heterogeneous, bool reverse) {
    World w(settings);
    btSoftBody *top, *bottom;
    if (reverse) {
        bottom = w.Sheet(-.4, 2, .25);
        top = w.Sheet(.4, 1, -1);
    } else {
        top = w.Sheet(.4, 1, -1);
        bottom = w.Sheet(-.4, 2, .25);
    }
    if (heterogeneous) {
        Nonuniform(*top, 1);
        Nonuniform(*bottom, 2);
    }
    Trace trace(w);
    bool finite = true;
    for (int i = 0; i < w.Steps(.8); ++i) {
        w.Step();
        finite &= trace.Add(w);
    }
    const double gap = double(MeanZ(*top) - MeanZ(*bottom));
    const bool interaction = gap > .02 && SoftMomentum(*top).z() > -.9;
    const bool pass = finite && trace.Conservative() && interaction;
    std::printf("world sheets nonuniform=%d reverse=%d dt=%.9f projection=%d gap=%.6f "
                "max_P=%.9g max_L=%.9g max_energy_gain=%.9g mass_error=%.3g primary_P=%.3g "
                "secondary_P=%.3g max_recipient_I=%.6g %s\n",
                heterogeneous, reverse, double(settings.dt), settings.projection, gap, trace.p,
                trace.l, trace.gain, trace.mass, trace.primaryBalance, trace.secondaryBalance,
                trace.maxRecipientImpulse, pass ? "PASS" : "FAIL");
    return pass;
}

struct Motion {
    btVector3 position, velocity;
};

bool ThreeWay(Settings settings, Motion *result = nullptr) {
    World w(settings);
    auto upper = w.Sheet(0, 20, 0), lower = w.Sheet(-.12, 10, 0);
    auto ball = w.Body(new btSphereShape(.2), 1, {.13, -.07, .6}, {0, 0, -5});
    Trace trace(w);
    bool finite = true;
    const auto &first = upper->m_faces[0];
    const btScalar winding =
        (first.m_n[1]->m_x - first.m_n[0]->m_x).cross(first.m_n[2]->m_x - first.m_n[0]->m_x).z() < 0
            ? -1
            : 1;
    double minimumClearance = 1e30;
    for (int i = 0; i < w.Steps(.8); ++i) {
        w.Step();
        finite &= trace.Add(w);
        const auto clearance =
            SphereClearance(*upper, ball->getWorldTransform().getOrigin(), .2, winding);
        finite &= std::isfinite(clearance);
        minimumClearance = std::min(minimumClearance, clearance);
    }
    const bool interaction = SoftMomentum(*lower).z() < -.01 && minimumClearance > -.02;
    const bool pass = finite && trace.Conservative() && interaction;
    if (result)
        *result = {ball->getWorldTransform().getOrigin(), ball->getLinearVelocity()};
    std::printf("world three-way dt=%.9f projection=%d max_P=%.9g max_L=%.9g "
                "max_energy_gain=%.9g min_clearance=%.6f lower_Pz=%.6f primary_P=%.3g "
                "secondary_P=%.3g max_recipient_I=%.6g %s\n",
                double(settings.dt), settings.projection, trace.p, trace.l, trace.gain,
                minimumClearance, double(SoftMomentum(*lower).z()), trace.primaryBalance,
                trace.secondaryBalance, trace.maxRecipientImpulse, pass ? "PASS" : "FAIL");
    return pass;
}

int main(int argc, char **argv) {
    const bool explicitForces = argc == 2 && !std::strcmp(argv[1], "--explicit-forces");
    if (argc != 1 && !explicitForces) {
        std::fputs("Usage: step-probe [--explicit-forces]\n", stderr);
        return 2;
    }
    int count = 0, failures = 0;
    for (bool projection : {false, true}) {
        // The explicit policy deliberately supports only the nonprojected
        // contact configuration; projected strain damping changes angular motion.
        if (explicitForces && projection)
            continue;
        for (int rate : {480, 960, 1920}) {
            Settings settings;
            settings.explicitForces = explicitForces;
            settings.projection = projection;
            settings.dt = btScalar(1) / rate;
            for (bool reverse : {false, true}) {
                for (bool heterogeneous : {false, true}) {
                    for (btScalar mass : {btScalar(2), btScalar(20)}) {
                        for (int location : {0, 1}) {
                            ++count;
                            if (!RigidSheet(settings, mass, heterogeneous, reverse, false,
                                            location))
                                ++failures;
                        }
                    }
                    ++count;
                    if (!Sheets(settings, heterogeneous, reverse))
                        ++failures;
                }
            }
            ++count;
            if (!RigidSheet(settings, 20, false, false, true))
                ++failures;
            ++count;
            if (!ThreeWay(settings))
                ++failures;
        }
    }
    if (explicitForces) {
        Motion motion[3];
        for (int i = 0; i < 3; ++i) {
            Settings settings;
            settings.explicitForces = true;
            settings.dt = btScalar(1) / (1920 << i);
            ++count;
            if (!ThreeWay(settings, &motion[i]))
                ++failures;
        }
        const auto dx = (motion[2].position - motion[1].position).length();
        const auto dv = (motion[2].velocity - motion[1].velocity).length();
        // Declared physical refinement bounds: 1 cm and 1% of incident 5 m/s.
        const bool refined = dx < .01 && dv < .05;
        ++count;
        if (!refined)
            ++failures;
        std::printf("three-way refinement 3840->7680Hz: dx=%.9g m dv=%.9g m/s %s\n", double(dx),
                    double(dv), refined ? "PASS" : "FAIL");
    }
    std::printf("world-step matrix: %d/%d passed\n", count - failures, count);
    return failures ? 1 : 0;
}
