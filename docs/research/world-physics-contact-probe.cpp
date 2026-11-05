// Diagnostic for Bullet d3afe761's deformable face/node repulsion operation.
// A prescribed normal impulse must balance even when face/node masses differ.
// This is not a replacement collision solver or a production integration.
#include <BulletSoftBody/btSoftBody.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

btVector3 Momentum(const btSoftBody &body, bool angular) {
    btVector3 sum(0, 0, 0);
    for (int i = 0; i < body.m_nodes.size(); ++i) {
        const auto &node = body.m_nodes[i];
        if (node.m_im > 0) {
            const btVector3 p = node.m_v / node.m_im;
            sum += angular ? node.m_x.cross(p) : p;
        }
    }
    return sum;
}

btScalar KineticEnergy(const btSoftBody &body) {
    btScalar energy = 0;
    for (int i = 0; i < body.m_nodes.size(); ++i)
        if (body.m_nodes[i].m_im > 0)
            energy += btScalar(.5) * body.m_nodes[i].m_v.length2() / body.m_nodes[i].m_im;
    return energy;
}

bool Case(const char *name, btScalar faceNodeMass) {
    btSoftBodyWorldInfo info;
    const btVector3 facePositions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const btVector3 nodePosition[] = {{.3, .5, .02}};
    const btScalar faceMass[] = {faceNodeMass, faceNodeMass, faceNodeMass};
    const btScalar nodeMass[] = {1};
    btSoftBody face(&info, 3, facePositions, faceMass);
    btSoftBody node(&info, 1, nodePosition, nodeMass);
    face.appendFace(0, 1, 2);
    node.m_nodes[0].m_v = {0, 0, -1};
    for (int i = 0; i < 3; ++i) {
        face.m_nodes[i].m_v.setZero();
        face.m_nodes[i].m_constrained = false;
    }
    node.m_nodes[0].m_constrained = false;
    btSoftBody::DeformableFaceNodeContact contact{};
    contact.m_face = &face.m_faces[0];
    contact.m_node = &node.m_nodes[0];
    contact.m_bary = {.2, .3, .5};
    contact.m_normal = {0, 0, 1};
    contact.m_margin = .03;
    contact.m_friction = 0;
    // The selected face/node operation owns this contact list; no world stepping,
    // other contacts, gravity, springs, sleeping or damping can mask the result.
    face.m_faceNodeContacts.push_back(contact);
    const auto initial = Momentum(face, false) + Momentum(node, false);
    const auto initialAngular = Momentum(face, true) + Momentum(node, true);
    const auto initialEnergy = KineticEnergy(face) + KineticEnergy(node);
    face.applyRepulsionForce(btScalar(1) / 480, false);
    const auto error = Momentum(face, false) + Momentum(node, false) - initial;
    const auto angularError = Momentum(face, true) + Momentum(node, true) - initialAngular;
    const auto finalEnergy = KineticEnergy(face) + KineticEnergy(node);

    // Independent reference: use the same scalar impulse magnitude, but apply
    // barycentric shares with each recipient's own inverse mass. Sum b_i = 1;
    // sum(m_i * delta_v_i) = J; angular impulse acts at the interpolated point.
    const btScalar j = -1 / (1 + contact.m_bary.length2());
    const btVector3 measuredNodeImpulse = node.m_nodes[0].m_v - btVector3(0, 0, -1);
    btScalar shareError = (measuredNodeImpulse + btVector3(0, 0, j)).length();
    btVector3 referenceP(0, 0, -j), referenceL = nodePosition[0].cross(referenceP);
    for (int i = 0; i < 3; ++i) {
        const btVector3 share(0, 0, contact.m_bary[i] * j);
        shareError = std::max(shareError, (face.m_nodes[i].m_v / face.m_nodes[i].m_im - share).length());
        referenceP += share;
        referenceL += facePositions[i].cross(share);
    }
    // Independent frictionless, zero-restitution effective-mass reference.
    // The minimal mass-operand patch above does not correct impulse magnitude.
    btScalar inverseEffectiveMass = 1;
    for (int i = 0; i < 3; ++i)
        inverseEffectiveMass += contact.m_bary[i] * contact.m_bary[i] / faceMass[i];
    const btScalar referenceJ = -1 / inverseEffectiveMass;
    btScalar inelasticEnergy = btScalar(.5) * (-1 - referenceJ) * (-1 - referenceJ);
    for (int i = 0; i < 3; ++i)
        inelasticEnergy += btScalar(.5) * contact.m_bary[i] * contact.m_bary[i] *
                           referenceJ * referenceJ / faceMass[i];
    const double p = double(error.length()), l = double(angularError.length());
    const bool pass = p < 1e-10 && l < 1e-10 && referenceP.length() < 1e-10 &&
                      referenceL.length() < 1e-10 && finalEnergy <= initialEnergy + 1e-10 &&
                      shareError < 1e-10 && measuredNodeImpulse.length() > .01 &&
                      inelasticEnergy <= initialEnergy + 1e-10 &&
                      std::isfinite(p + l + double(finalEnergy));
    std::printf("single-contact %s: momentum_error=%.9f Ns angular_error=%.9f Nms "
                "reference_error=%.9f Ns reference_angular_error=%.9f Nms "
                "impulse_share_error=%.9f Ns initial_energy=%.9f J final_energy=%.9f J "
                "inelastic_reference_energy=%.9f J %s\n",
                name, p, l, double(referenceP.length()), double(referenceL.length()),
                double(shareError), double(initialEnergy), double(finalEnergy), double(inelasticEnergy),
                pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    bool equal = Case("equal", 1), heavy = Case("heavy-face", 2), light = Case("light-face", .1);
    return equal && heavy && light ? 0 : 1;
}
