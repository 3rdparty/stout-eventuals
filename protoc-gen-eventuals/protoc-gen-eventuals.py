#!/usr/bin/env python3
import os

from google.protobuf.descriptor_pb2 import FileDescriptorProto

from pyprotoc_plugin.helpers import load_template, add_template_path
from pyprotoc_plugin.plugins import ProtocPlugin


class EventualsProtocPlugin(ProtocPlugin):

    def analyze_file(self, proto_file: FileDescriptorProto) -> dict:
        return {
            'proto_file_name':
            proto_file.name,
            'package_name':
            proto_file.package,
            'services': [{
                'name':
                service.name,
                'methods': [{
                    'name':
                    method.name,
                    'input_type':
                    method.input_type,
                    'output_type':
                    method.output_type,
                    'client_streaming':
                    getattr(method, 'client_streaming', False),
                    'server_streaming':
                    getattr(method, 'server_streaming', False),
                } for method in service.method],
            } for service in proto_file.service]
        }

    def process_server_methods(self, template_name, template_data, folder_for_generated_files):
        template = load_template(template_name)

        for service in template_data['services']:
            for method in service['methods']:

                service_data = {
                    'service': service,
                    'method': method,
                }

                content_server = template.render(**template_data, **service_data)
                file_name = service['name'] + '-' + method[
                    'name'] + '.eventuals.cc'
                self.response.file.add(name=folder_for_generated_files + '/' +
                        file_name,
                        content=content_server)


    def process_client_server_base(self, general_outputs, template_data):
        for file_name, template_name in general_outputs:
            template = load_template(template_name)
            content = template.render(**template_data)
            self.response.file.add(name=file_name, content=content)


    def process_file(self, proto_file: FileDescriptorProto):
        if proto_file.package.startswith('google'):
            return

        file_digest = self.analyze_file(proto_file)

        proto_file_path = file_digest.pop('proto_file_name')

        proto_file_name = proto_file_path.split('/')[-1]

        folder_for_generated_files = proto_file_name.replace(
            '.proto', '_generated')

        header_file_name = folder_for_generated_files + '/' + proto_file_name.replace(
            '.proto', '.eventuals.h')
        source_file_name = folder_for_generated_files + '/' + proto_file_name.replace(
            '.proto', '.eventuals.cc')
        header_client_name = folder_for_generated_files + '/' + proto_file_name.replace(
            '.proto', '.client.eventuals.h')

        eventuals_data = {
            'eventuals_header': header_file_name.split('/')[-1],
            'grpc_pb_header': proto_file_path.replace('.proto', '.grpc.pb.h'),
            'pb_header': proto_file_path.replace('.proto', '.pb.h'),
            'client_header': header_client_name.split('/')[-1],
        }

        template_directory = os.listdir(os.environ.get('TEMPLATE_PATH', ''))

        generate_methods_template_name = 'grpc-method-eventuals.cc.j2'
        generate_client_template_name = 'eventuals-client.h.j2'

        template_data = dict(**eventuals_data, **file_digest)

        if generate_methods_template_name in template_directory:
            general_outputs = [
                (header_file_name, 'eventuals.h.j2'),
                (source_file_name, 'eventuals.cc.j2'),
            ]

            self.process_client_server_base(general_outputs, template_data)
            self.process_server_methods(generate_methods_template_name, template_data, folder_for_generated_files)

        if generate_client_template_name in template_directory:
            general_outputs = [(header_client_name, generate_client_template_name)]
            self.process_client_server_base(general_outputs, template_data)


if __name__ == '__main__':
    add_template_path(os.path.join(__file__, '../templates/'))

    EventualsProtocPlugin.execute()
