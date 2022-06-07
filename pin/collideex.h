#pragma once

class Surface;

class BumperHitCircle : public HitCircle
{
public:
   BumperHitCircle(const Vertex2D& c, const float r, const float zlow, const float zhigh)
      : HitCircle(c,r,zlow,zhigh)
   {
      m_bumperanim_hitEvent = true;
      m_bumperanim_ringAnimOffset = 0.0f;
      m_pbumper = nullptr;
   }

   virtual void Collide(const CollisionEvent& coll) override;

   Bumper *m_pbumper;

   Vertex3Ds m_bumperanim_hitBallPosition;
   float m_bumperanim_ringAnimOffset;
   bool m_bumperanim_hitEvent;
};

class SlingshotAnimObject : public AnimObject
{
public:
   virtual void Animate() override;

   U32 m_TimeReset; // Time at which to pull in slingshot, Zero means the slingshot is currently reset
   bool m_animations;
   bool m_iframe;
};

class LineSegSlingshot : public LineSeg
{
public:
   LineSegSlingshot(const Vertex2D& p1, const Vertex2D& p2, const float _zlow, const float _zhigh)
      : LineSeg(p1, p2, _zlow, _zhigh)
   {
      m_slingshotanim.m_iframe = false;
      m_slingshotanim.m_TimeReset = 0; // Reset
      m_doHitEvent = false;
      m_force = 0.f;
      m_EventTimeReset = 0;
      m_psurface = nullptr;
   }

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eLineSegSlingshot; }
   virtual void Collide(const CollisionEvent& coll) override;

   Surface *m_psurface;

   float m_force;
   unsigned int m_EventTimeReset;

   SlingshotAnimObject m_slingshotanim;

   bool m_doHitEvent;
};

class Hit3DPoly : public HitObject
{
public:
   Hit3DPoly(Vertex3Ds * const rgv, const int count); // pointer is copied and content deleted in dtor
   Hit3DPoly(const float x, const float y, const float z, const float r, const int sections); // creates a circular hit poly
   virtual ~Hit3DPoly();

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return e3DPoly; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void CalcHitBBox() override;

   void Init(Vertex3Ds * const rgv, const int count);

private:
   Vertex3Ds *m_rgv;
   Vertex3Ds m_normal;
   int m_cvertex;
};

// Note that HitTriangle ONLY does include the plane and barycentric test, but NOT the edge and vertex test,
// thus one has to add these separately per mesh
class HitTriangle : public HitObject
{
public:
   HitTriangle(const Vertex3Ds rgv[3]);    // vertices in counterclockwise order
   virtual ~HitTriangle() {}

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eTriangle; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void CalcHitBBox() override;

   bool IsDegenerate() const { return m_normal.IsZero(); }

   Vertex3Ds m_rgv[3];
   Vertex3Ds m_normal;
};


class HitPlane : public HitObject
{
public:
   HitPlane() {}
   HitPlane(const Vertex3Ds& normal, const float d)
      : m_normal(normal), m_d(d)
   {
   }

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return ePlane; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void CalcHitBBox() override {}  //!! TODO: this is needed if we want to put it in the quadtree, but then again impossible as infinite area

private:
   Vertex3Ds m_normal;
   float m_d;
};


class SpinnerMoverObject : public MoverObject
{
public:
   virtual void UpdateDisplacements(const float dtime) override;
   virtual void UpdateVelocities() override;

   virtual bool AddToList() const override { return true; }

   Spinner *m_pspinner;

   float m_anglespeed;
   float m_angle;
   float m_angleMax;
   float m_angleMin;
   float m_elasticity;
   float m_damping;
   bool m_visible;
};

class HitSpinner : public HitObject
{
public:
   HitSpinner(Spinner * const pspinner, const float height);

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eSpinner; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void Contact(CollisionEvent& coll, const float dtime) override { }
   virtual void CalcHitBBox() override;

   virtual MoverObject *GetMoverObject() override { return &m_spinnerMover; }

   LineSeg m_lineseg[2];

   SpinnerMoverObject m_spinnerMover;
};

class GateMoverObject : public MoverObject
{
public:
   virtual void UpdateDisplacements(const float dtime) override;
   virtual void UpdateVelocities() override;

   virtual bool AddToList() const override { return true; }

   Gate *m_pgate;

   float m_anglespeed;
   float m_angle;
   float m_angleMin, m_angleMax;
   float m_friction;
   float m_damping;
   float m_gravityfactor;
   bool m_visible;
   bool m_open;         // True if the table logic is opening the gate, not just the ball passing through
   bool m_forcedMove;   // True if the table logic is opening/closing the gate
   bool m_hitDirection; // For the direction of the little bounce-back
};

class HitGate : public HitObject
{
public:
   HitGate(Gate * const pgate, const float height);

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eGate; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void Contact(CollisionEvent& coll, const float dtime) override { }
   virtual void CalcHitBBox() override;

   virtual MoverObject *GetMoverObject() override { return &m_gateMover; }

   GateMoverObject m_gateMover;
   bool m_twoWay;

private:
   Gate *m_pgate;
   LineSeg m_lineseg[3];
};

class TriggerLineSeg : public LineSeg
{
public:
   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eTrigger; }
   virtual void Collide(const CollisionEvent& coll) override;

   Trigger *m_ptrigger;
};

class TriggerHitCircle : public HitCircle
{
public:
   TriggerHitCircle(const Vertex2D& c, const float r, const float zlow, const float zhigh) : HitCircle(c, r, zlow, zhigh)
   {
      m_ptrigger = nullptr;
   }

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return eTrigger; }
   virtual void Collide(const CollisionEvent& coll) override;

   Trigger *m_ptrigger;
};

// Arbitrary line segment in 3D space.
// Implemented by transforming a HitLineZ to the desired orientation.
class HitLine3D : public HitLineZ
{
public:
   HitLine3D(const Vertex3Ds& v1, const Vertex3Ds& v2);

   virtual float HitTest(const BallS& ball, const float dtime, CollisionEvent& coll) const override;
   virtual int GetType() const override { return e3DLine; }
   virtual void Collide(const CollisionEvent& coll) override;
   virtual void CalcHitBBox() override { } // already done in constructor

private:
   Matrix3 m_matrix;
};
