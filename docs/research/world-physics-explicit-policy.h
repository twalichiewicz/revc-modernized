#pragma once
// Research-only integration policy, not a general Bullet replacement solver.
// All spring/damping impulses are evaluated before the shared contact solve.
// Retain the accepted contact velocities instead of re-solving only the soft
// side afterward, which would omit the matching rigid-body reaction.
#include <BulletSoftBody/btDeformableBodySolver.h>
#include <BulletSoftBody/btDeformableMassSpringForce.h>
#include <stdexcept>

class ExplicitContactSolver : public btDeformableBodySolver {
  public:
    void setupDeformableSolve(bool implicit) override {
        if (implicit)
            throw std::logic_error("ExplicitContactSolver requires explicit force integration");
        btDeformableBodySolver::setupDeformableSolve(false);
    }
    void solveDeformableConstraints(btScalar) override {
        // setupDeformableSolve saved the contact delta and temporary split
        // velocity. This restores that delta without another unilateral solve.
        updateVelocity();
    }
};

class ExplicitSpringForce : public btDeformableMassSpringForce {
    btScalar stiffness, bendingStiffness;

  public:
    ExplicitSpringForce(btScalar k, btScalar damping, bool angular = true, double bending = -1)
        : btDeformableMassSpringForce(k, damping, angular, bending), stiffness(k),
          bendingStiffness(bending < 0 ? k : btScalar(bending)) {}
    void addScaledExplicitForce(btScalar scale, TVStack &force) override {
        // The predictor m_q may be stale after the previous contact step. The
        // spring acts along the current physical geometry, preserving torque.
        for (int i = 0; i < m_softBodies.size(); ++i) {
            const auto body = m_softBodies[i];
            if (!body->isActive())
                continue;
            for (int j = 0; j < body->m_links.size(); ++j) {
                const auto &link = body->m_links[j];
                const auto a = link.m_n[0], b = link.m_n[1];
                const auto direction = b->m_x - a->m_x;
                const auto unit =
                    direction.length() > SIMD_EPSILON ? direction.normalized() : btVector3(0, 0, 0);
                const auto k = link.m_bbending ? bendingStiffness : stiffness;
                const auto impulse = scale * k * (direction - unit * link.m_rl);
                force[a->index] += impulse;
                force[b->index] -= impulse;
            }
        }
        btDeformableMassSpringForce::addScaledDampingForce(scale, force);
    }
};
