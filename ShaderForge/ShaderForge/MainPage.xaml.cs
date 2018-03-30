﻿using Windows.UI;
using Windows.UI.Xaml.Controls;

namespace ShaderForge
{
    using rMind.CanvasEx;
    using rMind.Elements;
    using rMind.Content;
    using rMind.Nodes;

    using HLSL.Elements;
    using System;
    using HLSL.Editor.Elements.Base;
    using HLSL.Controls;

    public sealed partial class MainPage : Page
    {
        rMindCanvasController canvasController;
        HLSLNodeController rootController;

        public MainPage()
        {
            this.InitializeComponent();

            canvasController = new rMindCanvasController(canvas, scroll);
            rootController = new HLSLNodeController(canvasController);

            canvasController.SetController(rootController);
            CreateMaterialNode();

            canvasController.Draw();
            dx.StartRenderLoop();

            tree.SetRoot(HLSL.Editor.TreeBuilder.Build());
            tree.OnSelectItem += (item) => {
                var it = item as TreeSelectorItem;
                if (it != null)
                {
                    rootController.CreateItem(it.Type);
                }
            };
        }

        Material material;

        void CreateMaterialNode()
        {
            material = new Material(rootController)
            {
                Header = "Material",
                CanEdit = false
            };
            rootController.AddElement(material);

            rootController.AddElement(new ColorNode(rootController)
            {
                Header = "Color node",
                CanEdit = true
            });
        }

        private void OnAdd(object sender, Windows.UI.Xaml.RoutedEventArgs e)
        {
            rootController.AddElement(new MathNodeBase(rootController));
           
        }

        private void OnCompile(object sender, Windows.UI.Xaml.RoutedEventArgs e)
        {  
            try
            {
                var shader = material.GetPixelShader();
                dx.CompilePixelShader(shader);
            }
            catch(Exception ex)
            {
                var l = ex.Message;
            }
            
        }

        private void OnAddVector3(object sender, Windows.UI.Xaml.RoutedEventArgs e)
        {
            var vector = new Vector3Node(rootController);
            vector.Translate(new rMind.Types.Vector2(100, 100));
            rootController.AddElement(vector);
        }
    }
}
