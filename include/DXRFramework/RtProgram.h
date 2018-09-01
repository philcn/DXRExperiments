#pragma once

namespace DXRFramework
{
    class RtProgram
    {
    public:
        using SharedPtr = std::shared_ptr<RtProgram>;
        
        static SharedPtr create();
        ~RtProgram();

    private:
        RtProgram();

        // DXIL

        // ProgramList
            // RayGenProgram
            // MissProgramList
            // HitProgramList

        // Global root signature
    };
}
