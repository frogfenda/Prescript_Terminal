/*
【模块职责】把V4B切角机身的十个三维顶点旋转到入口人体坐标，再按固定斜视相机投影到屏幕。
【绘制策略】七个面按观察深度从远到近填充；只有+BodyZ正面朝向观察者时才覆盖青色屏幕，
因此设备翻面后屏幕会自然隐藏，不会穿透背壳显示。
*/
#include "ui/ui_pose_model.h"

#include "hal/hal.h"

namespace UIPoseModel
{
    namespace
    {
        struct Point3
        {
            float x;
            float y;
            float z;
        };

        struct Point2
        {
            int x;
            int y;
        };

        struct Face
        {
            uint8_t index[5];
            uint8_t count;
            uint16_t color;
            float depth;
        };

        Point3 Rotate(const SysPose::Quaternion &q, const Point3 &value)
        {
            const float tx = 2.0f * (q.y * value.z - q.z * value.y);
            const float ty = 2.0f * (q.z * value.x - q.x * value.z);
            const float tz = 2.0f * (q.x * value.y - q.y * value.x);
            return {
                value.x + q.w * tx + q.y * tz - q.z * ty,
                value.y + q.w * ty + q.z * tx - q.x * tz,
                value.z + q.w * tz + q.x * ty - q.y * tx,
            };
        }

        Point2 Project(const Point3 &point, int center_x, int center_y, float scale)
        {
            /*
             * 固定正交斜视相机：世界+Z稳定映射到屏幕上方，同时保留HumanX/HumanY两个水平方向的
             * 透视差。正面矩形转离观察者时，投影会自然露出右侧或顶侧厚度，形成实物示意中的斜线。
             */
            const float screen_x = 0.8944f * point.x + 0.4472f * point.y;
            const float screen_y = 0.2491f * point.x - 0.4983f * point.y - 0.8305f * point.z;
            return {
                center_x + static_cast<int>(screen_x * scale),
                center_y + static_cast<int>(screen_y * scale),
            };
        }

        float CameraDepth(const Point3 &point)
        {
            // 数值越大越靠近观察者；与Project使用的两个屏幕基向量正交。
            return 0.3714f * point.x - 0.7428f * point.y + 0.5571f * point.z;
        }

        void FillQuad(const Point2 &a,
                      const Point2 &b,
                      const Point2 &c,
                      const Point2 &d,
                      uint16_t color)
        {
            HAL_Fill_Triangle(a.x, a.y, b.x, b.y, c.x, c.y, color);
            HAL_Fill_Triangle(a.x, a.y, c.x, c.y, d.x, d.y, color);
        }

        void DrawQuadOutline(const Point2 &a,
                             const Point2 &b,
                             const Point2 &c,
                             const Point2 &d,
                             uint16_t color)
        {
            HAL_Draw_Line(a.x, a.y, b.x, b.y, color);
            HAL_Draw_Line(b.x, b.y, c.x, c.y, color);
            HAL_Draw_Line(c.x, c.y, d.x, d.y, color);
            HAL_Draw_Line(d.x, d.y, a.x, a.y, color);
        }

        /** 用第一个顶点作扇形中心填充四边形或五边形机身面。 */
        void FillFace(const Face &face, const Point2 *vertices)
        {
            const Point2 &origin = vertices[face.index[0]];
            for (uint8_t corner = 1; corner + 1 < face.count; ++corner)
            {
                const Point2 &b = vertices[face.index[corner]];
                const Point2 &c = vertices[face.index[corner + 1]];
                HAL_Fill_Triangle(origin.x, origin.y, b.x, b.y, c.x, c.y, face.color);
            }
        }

        void DrawFaceOutline(const Face &face, const Point2 *vertices, uint16_t color)
        {
            for (uint8_t corner = 0; corner < face.count; ++corner)
            {
                const Point2 &a = vertices[face.index[corner]];
                const Point2 &b = vertices[face.index[(corner + 1) % face.count]];
                HAL_Draw_Line(a.x, a.y, b.x, b.y, color);
            }
        }

        Point3 Interpolate(const Point3 &a, const Point3 &b, float amount)
        {
            return {
                a.x + (b.x - a.x) * amount,
                a.y + (b.y - a.y) * amount,
                a.z + (b.z - a.z) * amount,
            };
        }
    }

    void DrawDevice(const SysPose::Quaternion &orientation,
                    int center_x,
                    int center_y,
                    float scale)
    {
        /*
         * 正视图采用用户实物示意的横向比例：BodyX为约428px左右长边，BodyY为约168px顶底短边。
         * 右上角沿+X/+Y切去一角；两组旋转标签只确认物理轴和符号，外壳长宽/切角由实物确认。
         */
        constexpr float HALF_LENGTH_X = 1.75f;
        constexpr float HALF_WIDTH_Y = 0.68f;
        constexpr float HALF_THICKNESS_Z = 0.18f;
        constexpr float CORNER_CUT = 0.32f;

        // 0～4是背面轮廓，5～9是对应正面轮廓；正视时切角位于+BodyX/+BodyY右上角。
        const Point3 body_vertices[10] = {
            {-HALF_LENGTH_X, -HALF_WIDTH_Y, -HALF_THICKNESS_Z},
            {+HALF_LENGTH_X, -HALF_WIDTH_Y, -HALF_THICKNESS_Z},
            {+HALF_LENGTH_X, +HALF_WIDTH_Y - CORNER_CUT, -HALF_THICKNESS_Z},
            {+HALF_LENGTH_X - CORNER_CUT, +HALF_WIDTH_Y, -HALF_THICKNESS_Z},
            {-HALF_LENGTH_X, +HALF_WIDTH_Y, -HALF_THICKNESS_Z},
            {-HALF_LENGTH_X, -HALF_WIDTH_Y, +HALF_THICKNESS_Z},
            {+HALF_LENGTH_X, -HALF_WIDTH_Y, +HALF_THICKNESS_Z},
            {+HALF_LENGTH_X, +HALF_WIDTH_Y - CORNER_CUT, +HALF_THICKNESS_Z},
            {+HALF_LENGTH_X - CORNER_CUT, +HALF_WIDTH_Y, +HALF_THICKNESS_Z},
            {-HALF_LENGTH_X, +HALF_WIDTH_Y, +HALF_THICKNESS_Z},
        };

        Point3 world_vertices[10];
        Point2 projected_vertices[10];
        for (uint8_t index = 0; index < 10; ++index)
        {
            world_vertices[index] = Rotate(orientation, body_vertices[index]);
            projected_vertices[index] = Project(world_vertices[index], center_x, center_y, scale);
        }

        Face faces[7] = {
            {{0, 1, 2, 3, 4}, 5, TFT_DARKGREY, 0.0f},
            {{0, 1, 6, 5, 0}, 4, 0x4208, 0.0f},
            {{1, 2, 7, 6, 0}, 4, 0x5AEB, 0.0f},
            {{2, 3, 8, 7, 0}, 4, 0x632C, 0.0f},
            {{3, 4, 9, 8, 0}, 4, 0x39E7, 0.0f},
            {{4, 0, 5, 9, 0}, 4, 0x4A49, 0.0f},
            {{5, 6, 7, 8, 9}, 5, 0x6B6D, 0.0f},
        };
        for (uint8_t face_index = 0; face_index < 7; ++face_index)
        {
            float depth = 0.0f;
            for (uint8_t corner = 0; corner < faces[face_index].count; ++corner)
                depth += CameraDepth(world_vertices[faces[face_index].index[corner]]);
            faces[face_index].depth = depth / static_cast<float>(faces[face_index].count);
        }

        // 七个元素使用插入排序即可，固定数组避免在每帧姿态绘制中引入堆分配。
        for (uint8_t index = 1; index < 7; ++index)
        {
            const Face value = faces[index];
            int previous = index - 1;
            while (previous >= 0 && faces[previous].depth > value.depth)
            {
                faces[previous + 1] = faces[previous];
                --previous;
            }
            faces[previous + 1] = value;
        }

        for (uint8_t face_index = 0; face_index < 7; ++face_index)
        {
            const Face &face = faces[face_index];
            FillFace(face, projected_vertices);
            DrawFaceOutline(face, projected_vertices, TFT_LIGHTGREY);
        }

        const Point3 front_normal = Rotate(orientation, {0.0f, 0.0f, 1.0f});
        if (CameraDepth(front_normal) <= 0.0f)
            return;

        /*
         * 屏幕略高于正面并向四边内缩；除青色边框外画三条内容线，让小尺寸投影中仍能一眼识别
         * 哪一面是屏幕，而不是把正面误看成普通背板。
         */
        constexpr float SCREEN_HALF_LENGTH_X = 1.40f;
        constexpr float SCREEN_HALF_WIDTH_Y = 0.42f;
        constexpr float SCREEN_Z = HALF_THICKNESS_Z + 0.01f;
        const Point3 screen_body[4] = {
            {-SCREEN_HALF_LENGTH_X, -SCREEN_HALF_WIDTH_Y, SCREEN_Z},
            {+SCREEN_HALF_LENGTH_X, -SCREEN_HALF_WIDTH_Y, SCREEN_Z},
            {+SCREEN_HALF_LENGTH_X, +SCREEN_HALF_WIDTH_Y, SCREEN_Z},
            {-SCREEN_HALF_LENGTH_X, +SCREEN_HALF_WIDTH_Y, SCREEN_Z},
        };
        Point3 screen_world[4];
        Point2 screen_quad[4];
        for (uint8_t index = 0; index < 4; ++index)
        {
            screen_world[index] = Rotate(orientation, screen_body[index]);
            screen_quad[index] = Project(screen_world[index], center_x, center_y, scale);
        }
        FillQuad(screen_quad[0], screen_quad[1], screen_quad[2], screen_quad[3], TFT_NAVY);
        DrawQuadOutline(screen_quad[0], screen_quad[1], screen_quad[2], screen_quad[3], TFT_CYAN);

        for (uint8_t line = 1; line <= 3; ++line)
        {
            const float amount = static_cast<float>(line) * 0.22f;
            const Point3 left = Interpolate(screen_world[3], screen_world[0], amount);
            const Point3 right = Interpolate(screen_world[2], screen_world[1], amount);
            const Point2 left_2d = Project(left, center_x, center_y, scale);
            const Point2 right_2d = Project(right, center_x, center_y, scale);
            HAL_Draw_Line(left_2d.x, left_2d.y, right_2d.x, right_2d.y, TFT_CYAN);
        }
    }
}
