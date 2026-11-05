// Research fixture for pinned Bullet d3afe761; not linked into the game.
// Rigid breakage and elastic coupling are measured separately. This does not
// implement or establish plastic sheet yielding, tearing, or game integration.
#include <BulletDynamics/ConstraintSolver/btFixedConstraint.h>
#include <BulletSoftBody/btDeformableBodySolver.h>
#include <BulletSoftBody/btDeformableMassSpringForce.h>
#include <BulletSoftBody/btDeformableMultiBodyConstraintSolver.h>
#include <BulletSoftBody/btDeformableMultiBodyDynamicsWorld.h>
#include <BulletSoftBody/btSoftBodyHelpers.h>
#include <BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h>
#include <algorithm>
#include <btBulletDynamicsCommon.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "world-physics-explicit-policy.h"

struct Settings {
    const char *name = "baseline";
    btScalar dt = btScalar(1) / 480, damping = .1, erp = .06;
    bool split = true, implicit = false, projection = false, faceContact = true;
    bool explicitForces = false;
};
struct World {
    Settings settings;
    btSoftBodyRigidBodyCollisionConfiguration config;
    btCollisionDispatcher dispatcher{&config};
    btDbvtBroadphase broad;
    std::unique_ptr<btDeformableBodySolver> deformable;
    btDeformableMultiBodyConstraintSolver solver;
    std::unique_ptr<btDeformableMultiBodyDynamicsWorld> world;
    std::vector<std::unique_ptr<btCollisionShape>> shapes;
    std::vector<std::unique_ptr<btRigidBody>> rigid;
    std::vector<std::unique_ptr<btSoftBody>> soft;
    std::vector<std::unique_ptr<btDeformableMassSpringForce>> forces;
    explicit World(Settings options) : settings(options) {
        if (settings.explicitForces && (settings.implicit || settings.projection))
            throw std::invalid_argument("Explicit force policy requires nonprojected explicit stepping");
        if (settings.explicitForces) deformable = std::make_unique<ExplicitContactSolver>();
        else deformable = std::make_unique<btDeformableBodySolver>();
        solver.setDeformableSolver(deformable.get());
        world = std::make_unique<btDeformableMultiBodyDynamicsWorld>(&dispatcher, &broad, &solver,
                                                                     &config, deformable.get());
        world->setGravity({0, 0, 0});
        world->getWorldInfo().m_gravity = {0, 0, 0};
        world->getWorldInfo().air_density = 0;
        world->getSolverInfo().m_numIterations = 40;
        world->getSolverInfo().m_splitImpulse = settings.split;
        world->getSolverInfo().m_deformable_erp = settings.erp;
        world->setUseProjection(settings.projection);
        world->setImplicit(settings.implicit);
        world->setLineSearch(false);
    }
    ~World() {
        for (size_t i = 0; i < soft.size(); i++) {
            world->removeForce(soft[i].get(), forces[i].get());
            world->removeSoftBody(soft[i].get());
        }
        for (auto &body : rigid)
            world->removeRigidBody(body.get());
    }
    btRigidBody *Body(btCollisionShape *shape, btScalar mass, btVector3 at,
                      btVector3 velocity = {0, 0, 0}) {
        shapes.emplace_back(shape);
        btVector3 inertia(0, 0, 0);
        if (mass > 0)
            shape->calculateLocalInertia(mass, inertia);
        btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr, shape, inertia);
        auto body = std::make_unique<btRigidBody>(info);
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(at);
        // Keep the interpolation pose/inertia consistent too: deformable
        // predicted contacts read it even for an immovable obstacle.
        body->setCenterOfMassTransform(transform);
        body->setLinearVelocity(velocity);
        body->setFriction(0);
        body->setRestitution(0);
        body->setActivationState(DISABLE_DEACTIVATION);
        auto p = body.get();
        world->addRigidBody(p);
        rigid.push_back(std::move(body));
        return p;
    }
    btSoftBody *Sheet(btScalar z, btScalar mass, btScalar speed) {
        auto p = btSoftBodyHelpers::CreatePatch(world->getWorldInfo(), {-1, -1, z}, {1, -1, z},
                                                {-1, 1, z}, {1, 1, z}, 9, 9, 0, true);
        soft.emplace_back(p);
        p->setTotalMass(mass);
        p->getCollisionShape()->setMargin(.03);
        p->m_cfg.kDF = 0;
        p->m_cfg.collisions = btSoftBody::fCollision::SDF_RD | btSoftBody::fCollision::VF_DD;
        if (settings.faceContact)
            p->m_cfg.collisions |= btSoftBody::fCollision::SDF_RDF;
        p->setVelocity({0, 0, speed});
        p->setSpringStiffness(100);
        p->generateBendingConstraints(2);
        world->addSoftBody(p);
        if (settings.explicitForces)
            forces.push_back(std::make_unique<ExplicitSpringForce>(100, settings.damping, true));
        else forces.push_back(std::make_unique<btDeformableMassSpringForce>(100, settings.damping, true));
        world->addForce(p, forces.back().get());
        return p;
    }
    int Steps(double seconds) const { return int(std::lround(seconds / double(settings.dt))); }
    void Step() { world->stepSimulation(settings.dt, 0); }
};

btVector3 SoftMomentum(const btSoftBody &b) {
    btVector3 sum(0, 0, 0);
    for (int i = 0; i < b.m_nodes.size(); i++)
        if (b.m_nodes[i].m_im > 0)
            sum += b.m_nodes[i].m_v / b.m_nodes[i].m_im;
    return sum;
}
btScalar MeanZ(const btSoftBody &b) {
    btScalar sum = 0;
    for (int i = 0; i < b.m_nodes.size(); i++)
        sum += b.m_nodes[i].m_x.z();
    return sum / b.m_nodes.size();
}

bool Wing(Settings settings, bool strike) {
    World w(settings);
    auto body = w.Body(new btBoxShape({1, 2, .5}), 900, {0, 0, 0}, {0, 50, 0});
    auto wing = w.Body(new btBoxShape({3, .5, .1}), 100, {4, 0, 0}, {0, 50, 0});
    w.Body(new btBoxShape({.5, .5, 5}), 0, {strike ? 6.5 : 9, 8, 0});
    btTransform a, b;
    a.setIdentity();
    b.setIdentity();
    a.setOrigin({4, 0, 0});
    // Arbitrary threshold for a rigid baseline, not calibrated metal strength.
    btFixedConstraint joint(*body, *wing, a, b);
    joint.setBreakingImpulseThreshold(500);
    joint.enableFeedback(true);
    w.world->addConstraint(&joint, true);
    const auto initial = body->getLinearVelocity() * 900 + wing->getLinearVelocity() * 100;
    btVector3 reaction(0, 0, 0);
    bool broken = false;
    for (int step = 0; step < w.Steps(.5); step++) {
        w.Step();
        for (int i = 0; i < w.dispatcher.getNumManifolds(); i++) {
            auto m = w.dispatcher.getManifoldByIndexInternal(i);
            auto ca = static_cast<const btCollisionObject *>(m->getBody0()),
                 cb = static_cast<const btCollisionObject *>(m->getBody1());
            if (ca->isStaticObject() == cb->isStaticObject())
                continue;
            for (int k = 0; k < m->getNumContacts(); k++)
                reaction += m->getContactPoint(k).m_normalWorldOnB *
                            m->getContactPoint(k).getAppliedImpulse() *
                            (ca->isStaticObject() ? -1 : 1);
        }
        broken |= !joint.isEnabled();
    }
    auto total = body->getLinearVelocity() * 900 + wing->getLinearVelocity() * 100;
    double error = double((total - initial - reaction).length());
    double slowdown = 50 - double(body->getLinearVelocity().y());
    const bool pass = strike ? (broken && slowdown > 1e-4 && reaction.length() > 0 && error < 1)
                             : (!broken && std::fabs(slowdown) < 1e-6 && reaction.length() == 0);
    std::printf("rigid-wing %s: joint_broken=%d chassis_slowdown=%.6f m/s static_reaction=%.6f Ns "
                "momentum_error=%.9f Ns %s\n",
                strike ? "strike" : "miss", broken, slowdown, double(reaction.length()), error,
                pass ? "PASS" : "FAIL");
    w.world->removeConstraint(&joint);
    return pass;
}

bool RigidSoft(Settings settings) {
    World w(settings);
    auto sheet = w.Sheet(0, 20, 0);
    auto ball = w.Body(new btSphereShape(.2), 1, {0, 0, .6}, {0, 0, -5});
    const auto initial = ball->getLinearVelocity() + SoftMomentum(*sheet);
    for (int i = 0; i < w.Steps(.5); i++)
        w.Step();
    auto total = ball->getLinearVelocity() + SoftMomentum(*sheet);
    double error = double((total - initial).length());
    double centre = double(MeanZ(*sheet)), deformation = 0;
    for (int i = 0; i < sheet->m_nodes.size(); i++)
        deformation = std::max(deformation, std::fabs(double(sheet->m_nodes[i].m_x.z()) - centre));
    bool pass = SoftMomentum(*sheet).z() < -.01 && ball->getLinearVelocity().z() > -4.9 &&
                deformation > .01 && error < .05 && std::isfinite(deformation);
    std::printf("rigid-soft: projectile_vz=%.6f m/s sheet_pz=%.6f Ns elastic_shape_change=%.6f m "
                "momentum_error=%.9f Ns %s\n",
                double(ball->getLinearVelocity().z()), double(SoftMomentum(*sheet).z()),
                deformation, error, pass ? "PASS" : "FAIL");
    return pass;
}

bool SoftSoft(Settings settings, bool asymmetric) {
    World w(settings);
    auto top = w.Sheet(.4, 1, -1), bottom = w.Sheet(-.4, asymmetric ? 2 : 1, asymmetric ? .25 : 1);
    const auto initial = SoftMomentum(*top) + SoftMomentum(*bottom);
    for (int i = 0; i < w.Steps(.8); i++)
        w.Step();
    double separation = double(MeanZ(*top) - MeanZ(*bottom));
    double error = double((SoftMomentum(*top) + SoftMomentum(*bottom) - initial).length());
    bool pass = separation > .02 && SoftMomentum(*top).z() > -.9 && error < .05 &&
                std::isfinite(separation);
    std::printf("soft-soft %s: signed_separation=%.6f m top_pz=%.6f Ns momentum_error=%.9f Ns %s\n",
                asymmetric ? "asymmetric" : "symmetric", separation, double(SoftMomentum(*top).z()),
                error, pass ? "PASS" : "FAIL");
    return pass;
}

int main(int argc, char **argv) {
    Settings settings;
    if (argc == 2) {
        settings.name = argv[1];
        if (!std::strcmp(argv[1], "no-split"))
            settings.split = false;
        else if (!std::strcmp(argv[1], "no-damping"))
            settings.damping = 0;
        else if (!std::strcmp(argv[1], "node-only"))
            settings.faceContact = false;
        else if (!std::strcmp(argv[1], "implicit"))
            settings.implicit = true;
        else if (!std::strcmp(argv[1], "projection"))
            settings.projection = true;
        else if (!std::strcmp(argv[1], "zero-erp"))
            settings.erp = 0;
        else if (!std::strcmp(argv[1], "half-step"))
            settings.dt /= 2;
        else if (!std::strcmp(argv[1], "explicit-forces"))
            settings.explicitForces = true;
        else {
            std::fprintf(stderr, "Unknown diagnostic variant: %s\n", argv[1]);
            return 2;
        }
    } else if (argc != 1) {
        std::fprintf(stderr,
                     "Usage: probe "
                     "[no-split|no-damping|node-only|implicit|projection|zero-erp|half-step|explicit-forces]\n");
        return 2;
    }
    std::printf("variant=%s\n", settings.name);
    bool a = Wing(settings, false), b = Wing(settings, true), c = RigidSoft(settings);
    bool d = SoftSoft(settings, false), e = SoftSoft(settings, true);
    std::puts("Scope: CPU rigid breakage + elastic contact coupling only. Plasticity/fracture, "
              "angular CCD, real assets, city performance and game ownership are NOT validated.");
    return a && b && c && d && e ? 0 : 1;
}
