// Experimental Bullet contact regression. Not linked into macVC.
// Exercises the real upstream operation; expected impulses come from the
// frictionless/ Coulomb point-contact effective-mass equations and conservation.
#include "world-physics-explicit-policy.h"
#include <BulletSoftBody/btSoftBody.h>
#include <LinearMath/btQuaternion.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

bool CurrentGeometryForce() {
    btSoftBodyWorldInfo info;
    const btVector3 positions[] = {{0, 0, 0}, {1, 0, 0}};
    const btScalar mass[] = {1, 1};
    btSoftBody body(&info, 2, positions, mass);
    body.appendLink(0, 1);
    body.m_links[0].m_rl = .5;
    for (int i = 0; i < 2; ++i) {
        body.m_nodes[i].index = i;
        body.m_nodes[i].m_q = positions[i];
    }
    body.m_nodes[1].m_q = {1, .2, 0}; // stale predictor after an earlier collision
    ExplicitSpringForce spring(100, 0, true);
    spring.addSoftBody(&body);
    btDeformableLagrangianForce::TVStack force;
    force.resize(2, btVector3(0, 0, 0));
    spring.addScaledExplicitForce(1, force);
    const auto error = (force[0] - btVector3(50, 0, 0)).length();
    const auto torque = positions[0].cross(force[0]) + positions[1].cross(force[1]);
    const bool pass =
        error < 1e-9 && (force[0] + force[1]).length() < 1e-9 && torque.length() < 1e-9;
    std::printf("current-geometry force: force_error=%.9g N torque_error=%.9g Nm %s\n",
                double(error), double(torque.length()), pass ? "PASS" : "FAIL");
    return pass;
}

struct Case {
    const char *name;
    std::array<btScalar, 4> mass; // triangle vertices, then incident node; zero = fixed
    btVector3 incident, faceVelocity;
    btScalar friction;
    btScalar literalNormalImpulse; // checked for (.2,.3,.5); -1 skips this cross-check
};

struct Totals {
    btVector3 p{0, 0, 0}, l{0, 0, 0};
    btScalar energy = 0;
};

Totals Measure(const btSoftBody &face, const btSoftBody &incident) {
    Totals result;
    for (const btSoftBody *body : {&face, &incident}) {
        for (int i = 0; i < body->m_nodes.size(); ++i) {
            const auto &node = body->m_nodes[i];
            if (node.m_im <= 0)
                continue;
            const auto momentum = node.m_v / node.m_im;
            result.p += momentum;
            result.l += node.m_x.cross(momentum);
            result.energy += node.m_v.dot(momentum) * btScalar(.5);
        }
    }
    return result;
}

bool Contact(const Case &sample, int orientation, bool centroid) {
    const btMatrix3x3 rotation =
        orientation ? btMatrix3x3(btQuaternion(btVector3(1, 2, 3).normalized(), .7))
                    : btMatrix3x3::getIdentity();
    const btVector3 origin(2, -1, .5), normal = rotation * btVector3(0, 0, 1);
    const btVector3 bary = centroid ? btVector3(1. / 3, 1. / 3, 1. / 3) : btVector3(.2, .3, .5);
    const btVector3 positions[] = {origin, origin + rotation * btVector3(1, 0, 0),
                                   origin + rotation * btVector3(0, 1, 0)};
    // Coincident contact points eliminate an artificial friction couple across
    // a collision margin. Finite-thickness contact is a separate world test.
    const btVector3 point[] = {positions[0] * bary.x() + positions[1] * bary.y() +
                               positions[2] * bary.z()};
    btSoftBodyWorldInfo info;
    btSoftBody face(&info, 3, positions, sample.mass.data());
    btSoftBody incident(&info, 1, point, &sample.mass[3]);
    face.appendFace(0, 1, 2);
    std::array<btVector3, 4> before;
    std::array<btScalar, 4> inverse;
    for (int i = 0; i < 4; ++i) {
        auto &node = i == 3 ? incident.m_nodes[0] : face.m_nodes[i];
        inverse[i] = node.m_im;
        before[i] = rotation * (i == 3 ? sample.incident : sample.faceVelocity);
        node.m_v = before[i];
        node.m_constrained = false;
    }
    const auto initial = Measure(face, incident);
    btSoftBody::DeformableFaceNodeContact contact{};
    contact.m_node = &incident.m_nodes[0];
    contact.m_face = &face.m_faces[0];
    contact.m_normal = normal;
    contact.m_bary = bary;
    contact.m_margin = 0;
    contact.m_friction = sample.friction;
    face.m_faceNodeContacts.push_back(contact);

    // Independent oracle: Jn = -vn/(J M^-1 J^T), bounded Coulomb tangential J.
    const auto relative =
        before[3] - before[0] * bary.x() - before[1] * bary.y() - before[2] * bary.z();
    const auto tangent = relative - normal * relative.dot(normal);
    btScalar inverseEffectiveMass = inverse[3];
    for (int i = 0; i < 3; ++i)
        inverseEffectiveMass += bary[i] * bary[i] * inverse[i];
    btVector3 impulse(0, 0, 0);
    btScalar normalImpulse = 0;
    if (inverseEffectiveMass > 0 && relative.dot(normal) < 0) {
        normalImpulse = -relative.dot(normal) / inverseEffectiveMass;
        impulse = normal * normalImpulse;
        if (tangent.length() > 1e-12)
            impulse -= tangent.normalized() * std::min(sample.friction * normalImpulse,
                                                       tangent.length() / inverseEffectiveMass);
    }
    if (!centroid && sample.literalNormalImpulse >= 0 &&
        std::fabs(double(normalImpulse - sample.literalNormalImpulse)) > 1e-9) {
        std::printf("oracle literal mismatch: %s\n", sample.name);
        return false;
    }
    std::array<btVector3, 4> expected;
    for (int i = 0; i < 3; ++i)
        expected[i] = -impulse * bary[i];
    expected[3] = impulse;

    // No gravity, springs, world stepping, sleeping or post-step compensation.
    face.applyRepulsionForce(btScalar(1) / 480, false);
    const auto final = Measure(face, incident);
    btVector3 fixedImpulse(0, 0, 0), fixedAngular(0, 0, 0);
    btScalar shareError = 0, velocityError = 0, expectedEnergy = 0;
    for (int i = 0; i < 4; ++i) {
        const auto &node = i == 3 ? incident.m_nodes[0] : face.m_nodes[i];
        const auto wantedVelocity = before[i] + expected[i] * inverse[i];
        velocityError = std::max(velocityError, (node.m_v - wantedVelocity).length());
        if (inverse[i] > 0) {
            const auto measuredImpulse = (node.m_v - before[i]) / inverse[i];
            shareError = std::max(shareError, (measuredImpulse - expected[i]).length());
            expectedEnergy += wantedVelocity.length2() / inverse[i] * btScalar(.5);
        } else {
            fixedImpulse += expected[i];
            fixedAngular += node.m_x.cross(expected[i]);
        }
    }
    const auto momentumError = (final.p - initial.p + fixedImpulse).length();
    const auto angularError = (final.l - initial.l + fixedAngular).length();
    const auto energyError = btFabs(final.energy - expectedEnergy);
    const auto tolerance = btScalar(1e-9) * (1 + impulse.length() + initial.energy);
    const bool pass =
        momentumError < tolerance && angularError < tolerance && shareError < tolerance &&
        velocityError < tolerance && energyError < tolerance &&
        final.energy <= initial.energy + tolerance &&
        std::isfinite(double(momentumError + angularError + energyError + velocityError));
    std::printf("contact %-14s rotation=%d centroid=%d P=%.3g L=%.3g share=%.3g "
                "velocity=%.3g KE=%.9f->%.9f reference=%.9f %s\n",
                sample.name, orientation, centroid, double(momentumError), double(angularError),
                double(shareError), double(velocityError), double(initial.energy),
                double(final.energy), double(expectedEnergy), pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    const bool forceValid = CurrentGeometryForce();
    const Case cases[] = {
        {"equal", {1, 1, 1, 1}, {0, 0, -1}, {0, 0, 0}, 0, .7246376811594203},
        {"heavy-face", {2, 2, 2, 1}, {0, 0, -1}, {0, 0, 0}, 0, .8403361344537815},
        {"light-face", {.1, .1, .1, 1}, {0, 0, -1}, {0, 0, 0}, 0, .2083333333333333},
        {"mixed-face", {.1, 2, 5, 1}, {0, 0, -1}, {0, 0, 0}, 0, .6688963210702341},
        {"fixed-node", {1, 1, 1, 0}, {0, 0, 0}, {0, 0, 1}, 0, 2.631578947368421},
        {"fixed-face", {0, 0, 0, 1}, {0, 0, -1}, {0, 0, 0}, 0, 1},
        {"fixed-vertex", {0, .4, 2, 1}, {0, 0, -1}, {0, 0, 0}, 0, .7407407407407407},
        {"all-fixed", {0, 0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0, 0},
        {"separating", {.1, 2, 5, 1}, {1, 0, 1}, {0, 0, 0}, .4, 0},
        {"sliding", {.1, 2, 5, 1}, {3, 1, -1}, {0, 0, 0}, .4, .6688963210702341},
        {"sticking", {.1, 2, 5, 1}, {.1, .05, -1}, {0, 0, 0}, .8, .6688963210702341},
    };
    int count = 0, failures = 0;
    for (const auto &sample : cases)
        for (int orientation = 0; orientation < 2; ++orientation)
            for (bool centroid : {false, true}) {
                ++count;
                if (!Contact(sample, orientation, centroid))
                    ++failures;
            }
    std::printf("contact matrix: %d/%d passed\n", count - failures, count);
    return failures || !forceValid ? 1 : 0;
}
