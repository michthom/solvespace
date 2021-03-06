//-----------------------------------------------------------------------------
// The root function to paint our graphics window, after setting up all the
// views and such appropriately. Also contains all the stuff to manage the
// selection.
//
// Copyright 2008-2013 Jonathan Westhues.
//-----------------------------------------------------------------------------
#include "solvespace.h"

bool GraphicsWindow::Selection::Equals(Selection *b) {
    if(entity.v     != b->entity.v)     return false;
    if(constraint.v != b->constraint.v) return false;
    return true;
}

bool GraphicsWindow::Selection::IsEmpty() {
    if(entity.v)        return false;
    if(constraint.v)    return false;
    return true;
}

bool GraphicsWindow::Selection::HasEndpoints() {
    if(!entity.v) return false;
    Entity *e = SK.GetEntity(entity);
    return e->HasEndpoints();
}

void GraphicsWindow::Selection::Clear() {
    entity.v = constraint.v = 0;
    emphasized = false;
}

void GraphicsWindow::Selection::Draw(bool isHovered, Canvas *canvas) {
    const Camera &camera = canvas->GetCamera();

    std::vector<Vector> refs;
    if(entity.v) {
        Entity *e = SK.GetEntity(entity);
        e->Draw(isHovered ? Entity::DrawAs::HOVERED :
                            Entity::DrawAs::SELECTED,
                canvas);
        if(emphasized) {
            e->GetReferencePoints(&refs);
        }
    }
    if(constraint.v) {
        Constraint *c = SK.GetConstraint(constraint);
        c->Draw(isHovered ? Constraint::DrawAs::HOVERED :
                            Constraint::DrawAs::SELECTED,
                canvas);
        if(emphasized) {
            c->GetReferencePoints(camera, &refs);
        }
    }
    if(emphasized && (constraint.v || entity.v)) {
        // We want to emphasize this constraint or entity, by drawing a thick
        // line from the top left corner of the screen to the reference point(s)
        // of that entity or constraint.
        Canvas::Stroke strokeEmphasis = {};
        strokeEmphasis.layer  = Canvas::Layer::FRONT;
        strokeEmphasis.color  = Style::Color(Style::HOVERED).WithAlpha(50);
        strokeEmphasis.width  = 40;
        strokeEmphasis.unit   = Canvas::Unit::PX;
        Canvas::hStroke hcsEmphasis = canvas->GetStroke(strokeEmphasis);

        Point2d topLeftScreen;
        topLeftScreen.x = -(double)camera.width / 2;
        topLeftScreen.y = (double)camera.height / 2;
        Vector topLeft = camera.UnProjectPoint(topLeftScreen);

        auto it = std::unique(refs.begin(), refs.end(),
                              [](Vector a, Vector b) { return a.Equals(b); });
        refs.erase(it, refs.end());
        for(Vector p : refs) {
            canvas->DrawLine(topLeft, p, hcsEmphasis);
        }
    }
}

void GraphicsWindow::ClearSelection() {
    selection.Clear();
    SS.ScheduleShowTW();
    InvalidateGraphics();
}

void GraphicsWindow::ClearNonexistentSelectionItems() {
    bool change = false;
    Selection *s;
    selection.ClearTags();
    for(s = selection.First(); s; s = selection.NextAfter(s)) {
        if(s->constraint.v && !(SK.constraint.FindByIdNoOops(s->constraint))) {
            s->tag = 1;
            change = true;
        }
        if(s->entity.v && !(SK.entity.FindByIdNoOops(s->entity))) {
            s->tag = 1;
            change = true;
        }
    }
    selection.RemoveTagged();
    if(change) InvalidateGraphics();
}

//-----------------------------------------------------------------------------
// Is this entity/constraint selected?
//-----------------------------------------------------------------------------
bool GraphicsWindow::IsSelected(hEntity he) {
    Selection s = {};
    s.entity = he;
    return IsSelected(&s);
}
bool GraphicsWindow::IsSelected(Selection *st) {
    Selection *s;
    for(s = selection.First(); s; s = selection.NextAfter(s)) {
        if(s->Equals(st)) {
            return true;
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// Unselect an item, if it is selected. We can either unselect just that item,
// or also unselect any coincident points. The latter is useful if the user
// somehow selects two coincident points (like with select all), because it
// would otherwise be impossible to de-select the lower of the two.
//-----------------------------------------------------------------------------
void GraphicsWindow::MakeUnselected(hEntity he, bool coincidentPointTrick) {
    Selection stog = {};
    stog.entity = he;
    MakeUnselected(&stog, coincidentPointTrick);
}
void GraphicsWindow::MakeUnselected(Selection *stog, bool coincidentPointTrick){
    if(stog->IsEmpty()) return;

    Selection *s;

    // If an item was selected, then we just un-select it.
    selection.ClearTags();
    for(s = selection.First(); s; s = selection.NextAfter(s)) {
        if(s->Equals(stog)) {
            s->tag = 1;
        }
    }
    // If two points are coincident, then it's impossible to hover one of
    // them. But make sure to deselect both, to avoid mysterious seeming
    // inability to deselect if the bottom one did somehow get selected.
    if(stog->entity.v && coincidentPointTrick) {
        Entity *e = SK.GetEntity(stog->entity);
        if(e->IsPoint()) {
            Vector ep = e->PointGetNum();
            for(s = selection.First(); s; s = selection.NextAfter(s)) {
                if(!s->entity.v) continue;
                if(s->entity.v == stog->entity.v) continue;
                Entity *se = SK.GetEntity(s->entity);
                if(!se->IsPoint()) continue;
                if(ep.Equals(se->PointGetNum())) {
                    s->tag = 1;
                }
            }
        }
    }
    selection.RemoveTagged();
}

//-----------------------------------------------------------------------------
// Select an item, if it isn't selected already.
//-----------------------------------------------------------------------------
void GraphicsWindow::MakeSelected(hEntity he) {
    Selection stog = {};
    stog.entity = he;
    MakeSelected(&stog);
}

void GraphicsWindow::MakeSelected(hConstraint hc) {
    Selection stog = {};
    stog.constraint = hc;
    MakeSelected(&stog);
}

void GraphicsWindow::MakeSelected(Selection *stog) {
    if(stog->IsEmpty()) return;
    if(IsSelected(stog)) return;

    if(stog->entity.v != 0 && SK.GetEntity(stog->entity)->IsFace()) {
        // In the interest of speed for the triangle drawing code,
        // only two faces may be selected at a time.
        int c = 0;
        Selection *s;
        selection.ClearTags();
        for(s = selection.First(); s; s = selection.NextAfter(s)) {
            hEntity he = s->entity;
            if(he.v != 0 && SK.GetEntity(he)->IsFace()) {
                c++;
                if(c >= 2) s->tag = 1;
            }
        }
        selection.RemoveTagged();
    }

    selection.Add(stog);
}

//-----------------------------------------------------------------------------
// Select everything that lies within the marquee view-aligned rectangle.
//-----------------------------------------------------------------------------
void GraphicsWindow::SelectByMarquee() {
    Point2d marqueePoint = ProjectPoint(orig.marqueePoint);
    BBox marqueeBBox = BBox::From(Vector::From(marqueePoint.x, marqueePoint.y, -1),
                                  Vector::From(orig.mouse.x,   orig.mouse.y,    1));

    Entity *e;
    for(e = SK.entity.First(); e; e = SK.entity.NextAfter(e)) {
        if(e->group.v != SS.GW.activeGroup.v) continue;
        if(e->IsFace() || e->IsDistance()) continue;
        if(!e->IsVisible()) continue;

        bool entityHasBBox;
        BBox entityBBox = e->GetOrGenerateScreenBBox(&entityHasBBox);
        if(entityHasBBox && entityBBox.Overlaps(marqueeBBox)) {
            MakeSelected(e->h);
        }
    }
}

//-----------------------------------------------------------------------------
// Sort the selection according to various critieria: the entities and
// constraints separately, counts of certain types of entities (circles,
// lines, etc.), and so on.
//-----------------------------------------------------------------------------
void GraphicsWindow::GroupSelection() {
    gs = {};
    int i;
    for(i = 0; i < selection.n; i++) {
        Selection *s = &(selection.elem[i]);
        if(s->entity.v) {
            (gs.n)++;

            Entity *e = SK.entity.FindById(s->entity);

            if(e->IsStylable()) gs.stylables++;

            // A list of points, and a list of all entities that aren't points.
            if(e->IsPoint()) {
                gs.points++;
                gs.point.push_back(s->entity);
            } else {
                gs.entities++;
                gs.entity.push_back(s->entity);
            }

            // And an auxiliary list of normals, including normals from
            // workplanes.
            if(e->IsNormal()) {
                gs.anyNormals++;
                gs.anyNormal.push_back(s->entity);
            } else if(e->IsWorkplane()) {
                gs.anyNormals++;
                gs.anyNormal.push_back(e->Normal()->h);
            }

            // And of vectors (i.e., stuff with a direction to constrain)
            if(e->HasVector()) {
                gs.vectors++;
                gs.vector.push_back(s->entity);
            }

            // Faces (which are special, associated/drawn with triangles)
            if(e->IsFace()) {
                gs.faces++;
                gs.face.push_back(s->entity);
            }

            if(e->HasEndpoints()) {
                (gs.withEndpoints)++;
            }

            // And some aux counts too
            switch(e->type) {
                case Entity::Type::WORKPLANE:      (gs.workplanes)++; break;
                case Entity::Type::LINE_SEGMENT:   (gs.lineSegments)++; break;
                case Entity::Type::CUBIC:          (gs.cubics)++; break;
                case Entity::Type::CUBIC_PERIODIC: (gs.periodicCubics)++; break;

                case Entity::Type::ARC_OF_CIRCLE:
                    (gs.circlesOrArcs)++;
                    (gs.arcs)++;
                    break;

                case Entity::Type::CIRCLE:         (gs.circlesOrArcs)++; break;

                default: break;
            }
        }
        if(s->constraint.v) {
            gs.constraints++;
            gs.constraint.push_back(s->constraint);
            Constraint *c = SK.GetConstraint(s->constraint);
            if(c->IsStylable()) gs.stylables++;
            if(c->HasLabel()) gs.constraintLabels++;
        }
    }
}

Camera GraphicsWindow::GetCamera() const {
    Camera camera = {};
    camera.width     = (int)width;
    camera.height    = (int)height;
    camera.offset    = offset;
    camera.projUp    = projUp;
    camera.projRight = projRight;
    camera.scale     = scale;
    camera.tangent   = SS.CameraTangent();
    camera.hasPixels = true;
    return camera;
}

Lighting GraphicsWindow::GetLighting() const {
    Lighting lighting = {};
    lighting.backgroundColor   = SS.backgroundColor;
    lighting.ambientIntensity  = SS.ambientIntensity;
    lighting.lightIntensity[0] = SS.lightIntensity[0];
    lighting.lightIntensity[1] = SS.lightIntensity[1];
    lighting.lightDirection[0] = SS.lightDir[0];
    lighting.lightDirection[1] = SS.lightDir[1];
    return lighting;
}

void GraphicsWindow::HitTestMakeSelection(Point2d mp) {
    Selection s = {};

    // Did the view projection change? If so, invalidate bounding boxes.
    if(!offset.EqualsExactly(cached.offset) ||
           !projRight.EqualsExactly(cached.projRight) ||
           !projUp.EqualsExactly(cached.projUp) ||
           EXACT(scale != cached.scale)) {
        cached.offset = offset;
        cached.projRight = projRight;
        cached.projUp = projUp;
        cached.scale = scale;
        for(Entity *e = SK.entity.First(); e; e = SK.entity.NextAfter(e)) {
            e->screenBBoxValid = false;
        }
    }

    ObjectPicker canvas = {};
    canvas.camera    = GetCamera();
    canvas.selRadius = 10.0;
    canvas.point     = mp;
    canvas.maxZIndex = -1;

    // Always do the entities; we might be dragging something that should
    // be auto-constrained, and we need the hover for that.
    for(Entity &e : SK.entity) {
        if(!e.IsVisible()) continue;

        // Don't hover whatever's being dragged.
        if(e.h.request().v == pending.point.request().v) {
            // The one exception is when we're creating a new cubic; we
            // want to be able to hover the first point, because that's
            // how we turn it into a periodic spline.
            if(!e.IsPoint()) continue;
            if(!e.h.isFromRequest()) continue;
            Request *r = SK.GetRequest(e.h.request());
            if(r->type != Request::Type::CUBIC) continue;
            if(r->extraPoints < 2) continue;
            if(e.h.v != r->h.entity(1).v) continue;
        }

        if(canvas.Pick([&]{ e.Draw(Entity::DrawAs::DEFAULT, &canvas); })) {
            s = {};
            s.entity = e.h;
        }
    }

    // The constraints and faces happen only when nothing's in progress.
    if(pending.operation == Pending::NONE) {
        // Constraints
        for(Constraint &c : SK.constraint) {
            if(!c.IsVisible()) continue;

            if(canvas.Pick([&]{ c.Draw(Constraint::DrawAs::DEFAULT, &canvas); })) {
                s = {};
                s.constraint = c.h;
            }
        }

        // Faces, from the triangle mesh; these are lowest priority
        if(s.constraint.v == 0 && s.entity.v == 0 && showShaded && showFaces) {
            Group *g = SK.GetGroup(activeGroup);
            SMesh *m = &(g->displayMesh);

            uint32_t v = m->FirstIntersectionWith(mp);
            if(v) {
                s.entity.v = v;
            }
        }
    }

    canvas.Clear();

    if(!s.Equals(&hover)) {
        hover = s;
        PaintGraphics();
    }
}

//-----------------------------------------------------------------------------
// Project a point in model space to screen space, exactly as gl would; return
// units are pixels.
//-----------------------------------------------------------------------------
Point2d GraphicsWindow::ProjectPoint(Vector p) {
    Vector p3 = ProjectPoint3(p);
    Point2d p2 = { p3.x, p3.y };
    return p2;
}
//-----------------------------------------------------------------------------
// Project a point in model space to screen space, exactly as gl would; return
// units are pixels. The z coordinate is also returned, also in pixels.
//-----------------------------------------------------------------------------
Vector GraphicsWindow::ProjectPoint3(Vector p) {
    double w;
    Vector r = ProjectPoint4(p, &w);
    return r.ScaledBy(scale/w);
}
//-----------------------------------------------------------------------------
// Project a point in model space halfway into screen space. The scale is
// not applied, and the perspective divide isn't applied; instead the w
// coordinate is returned separately.
//-----------------------------------------------------------------------------
Vector GraphicsWindow::ProjectPoint4(Vector p, double *w) {
    p = p.Plus(offset);

    Vector r;
    r.x = p.Dot(projRight);
    r.y = p.Dot(projUp);
    r.z = p.Dot(projUp.Cross(projRight));

    *w = 1 + r.z*SS.CameraTangent()*scale;
    return r;
}

//-----------------------------------------------------------------------------
// Return a point in the plane parallel to the screen and through the offset,
// that projects onto the specified (x, y) coordinates.
//-----------------------------------------------------------------------------
Vector GraphicsWindow::UnProjectPoint(Point2d p) {
    Vector orig = offset.ScaledBy(-1);

    // Note that we ignoring the effects of perspective. Since our returned
    // point has the same component normal to the screen as the offset, it
    // will have z = 0 after the rotation is applied, thus w = 1. So this is
    // correct.
    orig = orig.Plus(projRight.ScaledBy(p.x / scale)).Plus(
                     projUp.   ScaledBy(p.y / scale));
    return orig;
}

Vector GraphicsWindow::UnProjectPoint3(Vector p) {
    p.z = p.z / (scale - p.z * SS.CameraTangent() * scale);
    double w = 1 + p.z * SS.CameraTangent() * scale;
    p.x *= w / scale;
    p.y *= w / scale;

    Vector orig = offset.ScaledBy(-1);
    orig = orig.Plus(projRight.ScaledBy(p.x)).Plus(
                     projUp.   ScaledBy(p.y).Plus(
                     projRight.Cross(projUp). ScaledBy(p.z)));
    return orig;
}

void GraphicsWindow::NormalizeProjectionVectors() {
    if(projRight.Magnitude() < LENGTH_EPS) {
        projRight = Vector::From(1, 0, 0);
    }

    Vector norm = projRight.Cross(projUp);
    // If projRight and projUp somehow ended up parallel, then pick an
    // arbitrary projUp normal to projRight.
    if(norm.Magnitude() < LENGTH_EPS) {
        norm = projRight.Normal(0);
    }
    projUp = norm.Cross(projRight);

    projUp = projUp.WithMagnitude(1);
    projRight = projRight.WithMagnitude(1);
}

void GraphicsWindow::DrawSnapGrid(Canvas *canvas) {
    if(!LockedInWorkplane()) return;

    hEntity he = ActiveWorkplane();
    EntityBase *wrkpl = SK.GetEntity(he),
               *norm  = wrkpl->Normal();
    Vector n = projUp.Cross(projRight);
    Vector wu, wv, wn, wp;
    wp = SK.GetEntity(wrkpl->point[0])->PointGetNum();
    wu = norm->NormalU();
    wv = norm->NormalV();
    wn = norm->NormalN();

    double g = SS.gridSpacing;

    double umin = VERY_POSITIVE, umax = VERY_NEGATIVE,
           vmin = VERY_POSITIVE, vmax = VERY_NEGATIVE;
    int a;
    for(a = 0; a < 4; a++) {
        // Ideally, we would just do +/- half the width and height; but
        // allow some extra slop for rounding.
        Vector horiz = projRight.ScaledBy((0.6*width)/scale  + 2*g),
               vert  = projUp.   ScaledBy((0.6*height)/scale + 2*g);
        if(a == 2 || a == 3) horiz = horiz.ScaledBy(-1);
        if(a == 1 || a == 3) vert  = vert. ScaledBy(-1);
        Vector tp = horiz.Plus(vert).Minus(offset);

        // Project the point into our grid plane, normal to the screen
        // (not to the grid plane). If the plane is on edge then this is
        // impossible so don't try to draw the grid.
        bool parallel;
        Vector tpp = Vector::AtIntersectionOfPlaneAndLine(
                                        wn, wn.Dot(wp),
                                        tp, tp.Plus(n),
                                        &parallel);
        if(parallel) return;

        tpp = tpp.Minus(wp);
        double uu = tpp.Dot(wu),
               vv = tpp.Dot(wv);

        umin = min(uu, umin);
        umax = max(uu, umax);
        vmin = min(vv, vmin);
        vmax = max(vv, vmax);
    }

    int i, j, i0, i1, j0, j1;

    i0 = (int)(umin / g);
    i1 = (int)(umax / g);
    j0 = (int)(vmin / g);
    j1 = (int)(vmax / g);

    if(i0 > i1 || i1 - i0 > 400) return;
    if(j0 > j1 || j1 - j0 > 400) return;

    Canvas::Stroke stroke = {};
    stroke.layer  = Canvas::Layer::BACK;
    stroke.color  = Style::Color(Style::DATUM).WithAlpha(75);
    stroke.unit   = Canvas::Unit::PX;
    stroke.width  = 1.0f;
    Canvas::hStroke hcs = canvas->GetStroke(stroke);

    for(i = i0 + 1; i < i1; i++) {
        canvas->DrawLine(wp.Plus(wu.ScaledBy(i*g)).Plus(wv.ScaledBy(j0*g)),
                         wp.Plus(wu.ScaledBy(i*g)).Plus(wv.ScaledBy(j1*g)),
                         hcs);
    }
    for(j = j0 + 1; j < j1; j++) {
        canvas->DrawLine(wp.Plus(wu.ScaledBy(i0*g)).Plus(wv.ScaledBy(j*g)),
                         wp.Plus(wu.ScaledBy(i1*g)).Plus(wv.ScaledBy(j*g)),
                         hcs);
    }
}

void GraphicsWindow::DrawPersistent(Canvas *canvas) {
    // Draw the active group; this does stuff like the mesh and edges.
    SK.GetGroup(activeGroup)->Draw(canvas);

    // Now draw the entities.
    for(Entity &e : SK.entity) {
        switch(SS.GW.drawOccludedAs) {
            case DrawOccludedAs::VISIBLE:
                e.Draw(Entity::DrawAs::OVERLAY, canvas);
                break;

            case DrawOccludedAs::STIPPLED:
                e.Draw(Entity::DrawAs::HIDDEN, canvas);
                /* fallthrough */
            case DrawOccludedAs::INVISIBLE:
                e.Draw(Entity::DrawAs::DEFAULT, canvas);
                break;
        }
    }

    // Draw filled paths in all groups, when those filled paths were requested
    // specially by assigning a style with a fill color, or when the filled
    // paths are just being filled by default. This should go last, to make
    // the transparency work.
    for(hGroup hg : SK.groupOrder) {
        Group *g = SK.GetGroup(hg);
        if(!(g->IsVisible())) continue;
        g->DrawFilledPaths(canvas);
    }
}

void GraphicsWindow::Draw(Canvas *canvas) {
    const Camera &camera = canvas->GetCamera();

    if(SS.bgImage.pixmap) {
        double mmw = SS.bgImage.pixmap->width  / SS.bgImage.scale,
               mmh = SS.bgImage.pixmap->height / SS.bgImage.scale;

        Vector n = camera.projUp.Cross(camera.projRight);
        Vector origin = SS.bgImage.origin;
        origin = origin.DotInToCsys(camera.projRight, camera.projUp, n);
        // Place the depth of our origin at the point that corresponds to
        // w = 1, so that it's unaffected by perspective.
        origin.z = (offset.ScaledBy(-1)).Dot(n);
        origin = origin.ScaleOutOfCsys(camera.projRight, camera.projUp, n);

        // Place the background at the very back of the Z order.
        Canvas::Fill fillBackground = {};
        fillBackground.color = RgbaColor::From(255, 255, 255, 255);
        fillBackground.layer = Canvas::Layer::BACK;
        Canvas::hFill hcfBackground = canvas->GetFill(fillBackground);

        canvas->DrawPixmap(SS.bgImage.pixmap,
                          origin, projRight.ScaledBy(mmw), projUp.ScaledBy(mmh),
                          { 0.0, 1.0 }, { 1.0, 0.0 },
                          hcfBackground);
    }

    // Nasty case when we're reloading the linked files; could be that
    // we get an error, so a dialog pops up, and a message loop starts, and
    // we have to get called to paint ourselves. If the sketch is screwed
    // up, then we could trigger an oops trying to draw.
    if(!SS.allConsistent) return;

    if(showSnapGrid) DrawSnapGrid(canvas);

    // Draw all the things that don't change when we rotate.
    DrawPersistent(canvas);

    // Draw the polygon errors.
    if(SS.checkClosedContour) {
        SK.GetGroup(activeGroup)->DrawPolyError(canvas);
    }

    // Draw the constraints
    for(Constraint &c : SK.constraint) {
        c.Draw(Constraint::DrawAs::DEFAULT, canvas);
    }

    // Draw the "pending" constraint, i.e. a constraint that would be
    // placed on a line that is almost horizontal or vertical.
    if(SS.GW.pending.operation == Pending::DRAGGING_NEW_LINE_POINT &&
            SS.GW.pending.hasSuggestion) {
        Constraint c = {};
        c.group = SS.GW.activeGroup;
        c.workplane = SS.GW.ActiveWorkplane();
        c.type = SS.GW.pending.suggestion;
        c.entityA = SS.GW.pending.request.entity(0);
        c.Draw(Constraint::DrawAs::DEFAULT, canvas);
    }

    Canvas::Stroke strokeAnalyze = Style::Stroke(Style::ANALYZE);
    strokeAnalyze.layer = Canvas::Layer::FRONT;
    Canvas::hStroke hcsAnalyze = canvas->GetStroke(strokeAnalyze);

    // Draw the traced path, if one exists
    SEdgeList tracedEdges = {};
    SS.traced.path.MakeEdgesInto(&tracedEdges);
    canvas->DrawEdges(tracedEdges, hcsAnalyze);
    tracedEdges.Clear();

    Canvas::Stroke strokeError = Style::Stroke(Style::DRAW_ERROR);
    strokeError.layer = Canvas::Layer::FRONT;
    strokeError.width = 12;
    Canvas::hStroke hcsError = canvas->GetStroke(strokeError);

    // And the naked edges, if the user did Analyze -> Show Naked Edges.
    canvas->DrawEdges(SS.nakedEdges, hcsError);

    // Then redraw whatever the mouse is hovering over, highlighted.
    hover.Draw(/*isHovered=*/true, canvas);
    SK.GetGroup(activeGroup)->DrawMesh(Group::DrawMeshAs::HOVERED, canvas);

    // And finally draw the selection, same mechanism.
    for(Selection *s = selection.First(); s; s = selection.NextAfter(s)) {
        s->Draw(/*isHovered=*/false, canvas);
    }
    SK.GetGroup(activeGroup)->DrawMesh(Group::DrawMeshAs::SELECTED, canvas);

    Canvas::Stroke strokeDatum = Style::Stroke(Style::DATUM);
    strokeDatum.unit  = Canvas::Unit::PX;
    strokeDatum.layer = Canvas::Layer::FRONT;
    strokeDatum.width = 1;
    Canvas::hStroke hcsDatum = canvas->GetStroke(strokeDatum);

    // An extra line, used to indicate the origin when rotating within the
    // plane of the monitor.
    if(SS.extraLine.draw) {
        canvas->DrawLine(SS.extraLine.ptA, SS.extraLine.ptB, hcsDatum);
    }

    // A note to indicate the origin in the just-exported file.
    if(SS.justExportedInfo.draw) {
        Vector p, u, v;
        if(SS.justExportedInfo.showOrigin) {
            p = SS.justExportedInfo.pt,
            u = SS.justExportedInfo.u,
            v = SS.justExportedInfo.v;
        } else {
            p = camera.offset.ScaledBy(-1);
            u = camera.projRight;
            v = camera.projUp;
        }
        canvas->DrawVectorText("previewing exported geometry; press Esc to return",
                              Style::DefaultTextHeight() / camera.scale,
                              p.Plus(u.ScaledBy(10/scale)).Plus(v.ScaledBy(10/scale)), u, v,
                              hcsDatum);

        if(SS.justExportedInfo.showOrigin) {
            Vector um = p.Plus(u.WithMagnitude(-15/scale)),
                   up = p.Plus(u.WithMagnitude(30/scale)),
                   vm = p.Plus(v.WithMagnitude(-15/scale)),
                   vp = p.Plus(v.WithMagnitude(30/scale));
            canvas->DrawLine(um, up, hcsDatum);
            canvas->DrawLine(vm, vp, hcsDatum);
            canvas->DrawVectorText("(x, y) = (0, 0) for file just exported",
                                  Style::DefaultTextHeight() / camera.scale,
                                  p.Plus(u.ScaledBy(40/scale)).Plus(
                                         v.ScaledBy(-(Style::DefaultTextHeight())/scale)), u, v,
                                  hcsDatum);
        }
    }
}

void GraphicsWindow::Paint() {
#if !defined(HEADLESS)
    havePainted = true;

    auto renderStartTime = std::chrono::high_resolution_clock::now();

    int w, h;
    GetGraphicsWindowSize(&w, &h);
    width = w;
    height = h;

    Camera camera = GetCamera();

    OpenGl1Renderer canvas = {};
    canvas.camera   = camera;
    canvas.lighting = GetLighting();

    if(!SS.ActiveGroupsOkay()) {
        // Draw a different background whenever we're having solve problems.
        RgbaColor bgColor = Style::Color(Style::DRAW_ERROR);
        bgColor = RgbaColor::FromFloat(0.4f*bgColor.redF(),
                                       0.4f*bgColor.greenF(),
                                       0.4f*bgColor.blueF());
        canvas.lighting.backgroundColor = bgColor;
        // And show the text window, which has info to debug it
        ForceTextWindowShown();
    }
    canvas.BeginFrame();
    canvas.UpdateProjection();

    Draw(&canvas);

    canvas.camera.LoadIdentity();
    canvas.UpdateProjection();

    UiCanvas uiCanvas = {};
    uiCanvas.canvas = &canvas;

    // If a marquee selection is in progress, then draw the selection
    // rectangle, as an outline and a transparent fill.
    if(pending.operation == Pending::DRAGGING_MARQUEE) {
        Point2d begin = ProjectPoint(orig.marqueePoint);
        uiCanvas.DrawRect((int)orig.mouse.x, (int)begin.x,
                          (int)orig.mouse.y, (int)begin.y,
                          /*fillColor=*/Style::Color(Style::HOVERED).WithAlpha(25),
                          /*outlineColor=*/Style::Color(Style::HOVERED));
    }

    // And finally the toolbar.
    if(SS.showToolbar) {
        canvas.camera.offset   = {};
        canvas.camera.offset.x = -(double)canvas.camera.width  / 2.0;
        canvas.camera.offset.y = -(double)canvas.camera.height / 2.0;
        canvas.UpdateProjection();
        ToolbarDraw(&uiCanvas);
    }

    // If we display UI elements, also display an fps counter.
    if(SS.showToolbar) {
        canvas.EndFrame();

        auto renderEndTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> renderTime = renderEndTime - renderStartTime;

        RgbaColor renderTimeColor;
        if(1000 / renderTime.count() < 60) {
            // We aim for a steady 60fps; draw the counter in red when we're slower.
            renderTimeColor = { 255, 0, 0, 255 };
        } else {
            renderTimeColor = { 255, 255, 255, 255 };
        }
        uiCanvas.DrawBitmapText(ssprintf("rendered in %ld ms (%ld 1/s)",
                                         (long)renderTime.count(),
                                         (long)(1000/renderTime.count())),
                                5, 5, renderTimeColor);
    }

    canvas.EndFrame();
    canvas.Clear();
#endif
}
